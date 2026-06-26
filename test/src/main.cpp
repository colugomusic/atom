#include <atom.hpp>
#include <format>
#include <iostream>

// This is just a smoke test against compile errors, crashes,
// assertion failures and memory leaks and also serves as a
// basic usage example.

static constexpr auto RUN_DURATION = std::chrono::seconds(10);

struct model_a {
	int counter = 0;
};

struct model_b {
	char c = 'A';
};

auto prev(char c) -> char {
	if (--c < '!') { c = '~'; }
	return c;
}

auto next(char c) -> char {
	if (++c > '~') { c = '!'; }
	return c;
}

constexpr auto fn_decrement_counter = [](model_a x) { x.counter--; return x; };
constexpr auto fn_increment_counter = [](model_a x) { x.counter++; return x; };
constexpr auto fn_prev_char         = [](model_b x) { x.c = prev(x.c); return x; };
constexpr auto fn_next_char         = [](model_b x) { x.c = next(x.c); return x; };

// Shows how apply an action to the model and return a value.
constexpr auto fn_just_return_42 = [](model_a x) {
	// ...could modify the model here...
	return std::make_pair(x, 42);
};

auto fn_set_counter(int v) { return [v](model_a x) { x.counter = v; return x; }; }

auto print_counter(const model_a& v) -> void { std::cout << std::format("counter: {}\n", v.counter); }
auto print_char(const model_b& v) -> void    { std::cout << std::format("character: {}\n", v.c); }

auto printer_thread(std::stop_token stop, atom::val<model_a>* a0, atom::val<model_a>* a1, atom::val<model_b>* b0, atom::val<model_b>* b1) -> void {
	while (!stop.stop_requested()) {
		print_counter(a0->get());
		print_counter(a1->get());
		print_char(b0->get());
		print_char(b1->get());
	}
}

auto counter_decrementer_thread(std::stop_token stop, atom::val<model_a>* a) -> void {
	while (!stop.stop_requested()) {
		a->apply(fn_decrement_counter);
	}
}

auto counter_incrementer_thread(std::stop_token stop, atom::val<model_a>* a) -> void {
	while (!stop.stop_requested()) {
		a->apply(fn_increment_counter);
	}
}

auto counter_setter_thread(std::stop_token stop, atom::val<model_a>* a) -> void {
	for (;;) {
		if (stop.stop_requested()) {
			break;
		}
		a->apply(fn_set_counter(0));
	}
}

auto return_42_thread(std::stop_token stop, atom::val<model_a>* a) -> void {
	while (!stop.stop_requested()) {
		const auto value = a->apply_r(fn_just_return_42);
		assert (value == 42);
	}
}

auto char_decrementer_thread(std::stop_token stop, atom::val<model_b>* b) -> void {
	while (!stop.stop_requested()) {
		b->apply(fn_prev_char);
	}
}

auto char_incrementer_thread(std::stop_token stop, atom::val<model_b>* b) -> void {
	while (!stop.stop_requested()) {
		b->apply(fn_next_char);
	}
}

auto main(int, const char*[]) -> int {
	// We create multiple vals of of the same <T> as they will be sharing
	// hazard pointer slots under the hood so we just want to check
	// everything is ok with that sharing.
	auto a0             = atom::val<model_a>{};
	auto a1             = atom::val<model_a>{};
	auto b0             = atom::val<model_b>{};
	auto b1             = atom::val<model_b>{};
	auto a0_counter_dec = std::jthread{counter_decrementer_thread, &a0};
	auto a0_counter_inc = std::jthread{counter_incrementer_thread, &a0};
	auto a0_counter_set = std::jthread{counter_setter_thread, &a0};
	auto a0_return_42   = std::jthread{return_42_thread, &a0};
	auto b0_char_dec    = std::jthread{char_decrementer_thread, &b0};
	auto b0_char_inc    = std::jthread{char_incrementer_thread, &b0};
	auto a1_counter_dec = std::jthread{counter_decrementer_thread, &a1};
	auto a1_counter_inc = std::jthread{counter_incrementer_thread, &a1};
	auto a1_counter_set = std::jthread{counter_setter_thread, &a1};
	auto a1_return_42   = std::jthread{return_42_thread, &a1};
	auto b1_char_dec    = std::jthread{char_decrementer_thread, &b1};
	auto b1_char_inc    = std::jthread{char_incrementer_thread, &b1};
	auto printer        = std::jthread{printer_thread, &a0, &a1, &b0, &b1};
	std::this_thread::sleep_for(RUN_DURATION);
	return 0;
}
