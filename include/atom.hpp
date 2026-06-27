#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

// Non-intrusive hazard pointers based partly on Daniel Anderson's implementation here:
// https://github.com/DanielLiamAnderson/atomic_shared_ptr/blob/master/include/parlay/details/hazard_pointers.hpp
//
// This is NOT a general-use hazard pointer implementation but it's a bit simpler than Anderson's so it might be
// useful as a reference.
//
// Unlike Anderson's implementation we generate a separate list of slots for every type T in atom::val<T>. This
// allows the implementation to be non-intrusive. This is intended to be used in a single-atom architecture where
// you would only want to protect one thing anyway (i.e. you probably only want to have one atom::val<T> in your
// program.) There is a little bit of memory overhead for each val of unique T that you instantiate.
namespace atom::hazptr {

static constexpr auto DEFAULT_GC_THRESHOLD = 2000;

template <typename T>
struct node {
	T value;
	node<T>* next = nullptr;
};

template <typename T>
struct retire_list {
	node<T>* head = nullptr;
};

template <typename T> using const_node_list = std::vector<const node<T>*>;
template <typename T> using node_list       = std::vector<node<T>*>;

template <typename T>
auto init_protected_node_list_for_gc(size_t count = std::max(1u, std::thread::hardware_concurrency()) * 2) -> const_node_list<T> {
	auto list = const_node_list<T>{};
	list.reserve(count);
	return list;
}

template <typename T>
auto init_node_pool(size_t count = std::max(1u, std::thread::hardware_concurrency())) -> node_list<T> {
	auto list = node_list<T>{};
	list.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		list.push_back(new node<T>{});
	}
	return list;
}

template <typename T>
auto delete_all(const node_list<T>& nodes) -> void {
	for (auto node : nodes) {
		delete node;
	}
}

template <typename T>
struct slot {
	slot() = default;
	~slot();
	hazptr::retire_list<T> retire_list;
	std::atomic<slot*> next                       = nullptr;
	std::atomic<bool> in_use                      = false; // Assigned to a thread?
	std::atomic<node<T>*> protected_node          = nullptr;
	int num_retires_since_gc                      = 0;
	const_node_list<T> protected_node_list_for_gc = init_protected_node_list_for_gc<T>();
	node_list<T> node_pool                        = init_node_pool<T>();
};

template <typename T>
struct hazptrs {
	hazptrs();
	~hazptrs();
	slot<T>* slot_list = nullptr;
};

template <typename T>
struct thread_slot {
	thread_slot(hazptr::hazptrs<T>* hazptrs);
	~thread_slot();
	auto get() { return slot_; }
private:
	slot<T>* slot_;
};

template <typename T>
auto g_hazptrs() -> std::shared_ptr<hazptrs<T>> {
	static auto g = std::make_shared<hazptrs<T>>();
	return g;
}

template <typename T>
auto tls_slot() -> slot<T>* {
	thread_local thread_slot<T> g{g_hazptrs<T>().get()};
	return g.get();
}

template <typename T>
auto push(hazptr::retire_list<T>* retire_list, hazptr::node<T>* node) -> void {
	node->next = std::exchange(retire_list->head, node);
}

template <typename T> [[nodiscard]]
auto init_list(std::size_t count) -> T* {
	T* head = nullptr;
	for (std::size_t i = 0; i < count; ++i) {
		auto new_item = new T{};
		new_item->next = head;
		head = new_item;
	}
	return head;
}

template <typename T>
auto delete_list(T* head) -> void {
	while (head) {
		auto old = std::exchange(head, head->next);
		delete old;
	}
}

template <typename T> [[nodiscard]]
auto init_slot_list(std::size_t count = std::max(1u, std::thread::hardware_concurrency())) -> slot<T>* {
	return init_list<slot<T>>(count);
}

template <typename T> [[nodiscard]]
auto create_new_item_at_tail(T* tail) -> T* {
	auto new_item = new T{};
	new_item->in_use.store(true);
	T* next = nullptr;
	while (!tail->next.compare_exchange_weak(next, new_item)) {
		tail = next;
		next = nullptr;
	}
	return new_item;
}

template <typename T> [[nodiscard]]
auto acquire_item_from_list(T* list) -> T* {
	auto curr = list;
	for (;;) {
		if (!curr->in_use.load() && !curr->in_use.exchange(true)) {
			return curr;
		}
		if (!curr->next.load()) {
			return create_new_item_at_tail(curr);
		}
		curr = curr->next.load();
	}
}

template <typename T> [[nodiscard]]
auto acquire_thread_slot(hazptr::hazptrs<T>* hazptrs) -> slot<T>* {
	assert (hazptrs);
	return acquire_item_from_list(hazptrs->slot_list);
}

template <typename T> [[nodiscard]]
auto acquire_node(node_list<T>* pool, T value) -> node<T>* {
	assert (pool);
	if (pool->empty()) {
		return new node<T>{std::move(value)};
	}
	auto node = pool->back();
	pool->pop_back();
	node->value = std::move(value);
	return node;
}

template <typename T> [[nodiscard]]
auto acquire_node(hazptr::slot<T>* slot, T value) -> node<T>* {
	assert (slot);
	return acquire_node(&slot->node_pool, std::move(value));
}

template <typename T> [[nodiscard]]
auto acquire_node(T value) -> node<T>* {
	return acquire_node(tls_slot<T>(), std::move(value));
}

template <typename T>
auto release_node(node_list<T>* pool, hazptr::node<T>* node) -> void {
	assert (pool);
	pool->push_back(node);
}

template <typename T>
auto release_node(hazptr::slot<T>* slot, hazptr::node<T>* node) -> void {
	assert (slot);
	release_node(&slot->node_pool, node);
}

template <typename T>
auto release_node(hazptr::node<T>* node) -> void {
	release_node(tls_slot<T>(), node);
}

template <typename T>
auto release_thread_slot(hazptr::slot<T>* slot) -> void {
	slot->in_use.store(false);
}

template <typename T, typename Fn>
auto for_each_slot(hazptrs<T>* hazptrs, Fn&& fn) -> void {
	assert (hazptrs);
	auto curr = hazptrs->slot_list;
	while (curr) {
		fn(*curr);
		curr = curr->next.load();
	}
}

template <typename T, typename Fn>
auto for_each_protected_node(hazptrs<T>* hazptrs, Fn&& fn) -> void {
	auto call_fn_if_protecting = [fn](const hazptr::slot<T>& slot) {
		if (const auto node = slot.protected_node.load()) {
			fn(node);
		}
	};
	for_each_slot(hazptrs, call_fn_if_protecting);
}

template <typename T, typename Alloc>
auto sort_and_remove_duplicates(std::vector<T, Alloc>* x) -> void {
	std::ranges::sort(*x);
	x->erase(std::unique(x->begin(), x->end()), x->end());
}

template <typename T>
auto find_protected_nodes(hazptr::hazptrs<T>* hazptrs, const_node_list<T>* list) -> void {
	assert (hazptrs);
	assert (list->empty());
	auto add_to_list = [list](const hazptr::node<T>* node) {
		list->push_back(node);
	};
	for_each_protected_node(hazptrs, add_to_list);
	sort_and_remove_duplicates(list);
}

template <typename T, typename FnIsProtected>
auto release_unprotected_nodes(hazptr::retire_list<T>* retire_list, node_list<T>* pool, FnIsProtected is_protected) -> void {
	while (retire_list->head && !is_protected(retire_list->head)) {
		auto old = std::exchange(retire_list->head, retire_list->head->next);
		release_node(pool, old);
	}
	if (retire_list->head) {
		auto prev = retire_list->head;
		auto curr = retire_list->head->next;
		while (curr) {
			if (!is_protected(curr)) {
				auto old = std::exchange(curr, curr->next);
				release_node(pool, old);
				prev->next = curr;
			}
			else {
				prev = std::exchange(curr, curr->next);
			}
		}
	}
}

template <typename T>
auto gc(hazptr::hazptrs<T>* hazptrs, hazptr::slot<T>* slot) -> void {
	assert (hazptrs);
	slot->num_retires_since_gc = 0;
	find_protected_nodes(hazptrs, &slot->protected_node_list_for_gc);
	auto is_protected = [slot](const hazptr::node<T>* node) {
		return std::ranges::binary_search(slot->protected_node_list_for_gc, node);
	};
	release_unprotected_nodes(&slot->retire_list, &slot->node_pool, is_protected);
	slot->protected_node_list_for_gc.clear();
}

template <typename T>
auto retire(hazptr::hazptrs<T>* hazptrs, hazptr::slot<T>* slot, hazptr::node<T>* node, int gc_threshold) -> void {
	assert (hazptrs);
	push(&slot->retire_list, node);
	if (++slot->num_retires_since_gc >= gc_threshold) {
		gc(hazptrs, slot);
	}
}

template <typename T>
auto protect(hazptr::slot<T>* slot, std::atomic<hazptr::node<T>*>& ptr_to_node) -> hazptr::node<T>* {
	assert (slot);
	auto node = ptr_to_node.load(std::memory_order_acquire);
	for (;;) {
		slot->protected_node.store(node);
		const auto check_value = ptr_to_node.load(std::memory_order_acquire);
		if (check_value == node) {
			return node;
		}
		node = check_value;
	}
	return node;
}

template <typename T>
auto release(hazptr::slot<T>* slot) -> void {
	assert (slot);
	slot->protected_node.store(nullptr, std::memory_order_release);
}

template <typename T>
auto retire(hazptr::node<T>* node, int gc_threshold) -> void {
	retire(g_hazptrs<T>().get(), tls_slot<T>(), node, gc_threshold);
}

template <typename T>
auto protect(std::atomic<hazptr::node<T>*>& ptr_to_node) -> hazptr::node<T>* {
	return protect(tls_slot<T>(), ptr_to_node);
}

template <typename T>
auto release() -> void {
	release(tls_slot<T>());
}

[[nodiscard]] auto fn_always(auto value) { return [value](auto&&...) { return value; }; }

template <typename T> hazptrs<T>::hazptrs() : slot_list{init_slot_list<T>()} {}
template <typename T> hazptrs<T>::~hazptrs() { delete_list(slot_list); }
template <typename T> thread_slot<T>::thread_slot(hazptr::hazptrs<T>* hazptrs) : slot_{acquire_thread_slot(hazptrs)} {}
template <typename T> thread_slot<T>::~thread_slot() { release_thread_slot(slot_); }
template <typename T> slot<T>::~slot() { release_unprotected_nodes(&retire_list, &node_pool, fn_always(false)); delete_all(node_pool); }

} // atom::hazptr

namespace atom {

template <typename T>
struct val {
	val(int gc_threshold = hazptr::DEFAULT_GC_THRESHOLD);
	~val();
	[[nodiscard]] auto get() const -> T;
	auto apply(auto fn) -> T;
	auto apply_r(auto fn) -> decltype(auto);
private:
	// We only hold this shared_ptr to keep hazptrs<T> alive
	// which allows clients to create val<T>s with static
	// storage duration without leaking memory at shutdown.
	std::shared_ptr<hazptr::hazptrs<T>> hazptrs_;
	mutable std::atomic<hazptr::node<T>*> node_;
	int gc_threshold_;
};

template <typename T>
val<T>::val(int gc_threshold)
	: hazptrs_{hazptr::g_hazptrs<T>()}
	, node_{hazptr::acquire_node<T>(T{})}
	, gc_threshold_{gc_threshold}
{
}

template <typename T>
val<T>::~val() {
	hazptr::retire(node_.load(std::memory_order_acquire), gc_threshold_);
}

template <typename T>
auto val<T>::apply(auto fn) -> T {
	for (;;) {
		auto expected_node = hazptr::protect(node_);
		auto old_value     = expected_node->value;
		hazptr::release<T>();
		const auto new_value    = fn(std::move(old_value));
		const auto desired_node = hazptr::acquire_node<T>(new_value);
		if (node_.compare_exchange_weak(expected_node, desired_node)) {
			hazptr::retire(expected_node, gc_threshold_);
			return new_value;
		}
		hazptr::release_node(desired_node);
	}
}

template <typename T>
auto val<T>::apply_r(auto fn) -> decltype(auto) {
	for (;;) {
		auto expected_node = hazptr::protect(node_);
		auto old_value     = expected_node->value;
		hazptr::release<T>();
		const auto [new_value, result] = fn(std::move(old_value));
		const auto desired_node        = hazptr::acquire_node<T>(new_value);
		if (node_.compare_exchange_weak(expected_node, desired_node)) {
			hazptr::retire(expected_node, gc_threshold_);
			return result;
		}
		hazptr::release_node(desired_node);
	}
}

template <typename T>
auto val<T>::get() const -> T {
	const auto node  = hazptr::protect(node_);
	const auto value = node->value;
	hazptr::release<T>();
	return value;
}

} // atom
