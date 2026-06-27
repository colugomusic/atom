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

struct profiling {
	int print_count       = 0;
	int counter_dec_count = 0;
	int counter_inc_count = 0;
	int counter_set_count = 0;
	int counter_ret_count = 0;
	int char_inc_count    = 0;
	int char_dec_count    = 0;
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

auto printer_thread(std::stop_token stop, atom::val<model_a>* a0, atom::val<model_a>* a1, atom::val<model_b>* b0, atom::val<model_b>* b1, profiling* prof) -> void {
	while (!stop.stop_requested()) {
		print_counter(a0->get());
		print_counter(a1->get());
		print_char(b0->get());
		print_char(b1->get());
		prof->print_count++;
	}
}

auto counter_decrementer_thread(std::stop_token stop, atom::val<model_a>* a, profiling* prof) -> void {
	while (!stop.stop_requested()) {
		a->apply(fn_decrement_counter);
		prof->counter_dec_count++;
	}
}

auto counter_incrementer_thread(std::stop_token stop, atom::val<model_a>* a, profiling* prof) -> void {
	while (!stop.stop_requested()) {
		a->apply(fn_increment_counter);
		prof->counter_inc_count++;
	}
}

auto counter_setter_thread(std::stop_token stop, atom::val<model_a>* a, profiling* prof) -> void {
	while (!stop.stop_requested()) {
		a->apply(fn_set_counter(0));
		prof->counter_set_count++;
	}
}

auto return_42_thread(std::stop_token stop, atom::val<model_a>* a, profiling* prof) -> void {
	while (!stop.stop_requested()) {
		const auto value = a->apply_r(fn_just_return_42);
		assert (value == 42);
		prof->counter_ret_count++;
	}
}

auto char_decrementer_thread(std::stop_token stop, atom::val<model_b>* b, profiling* prof) -> void {
	while (!stop.stop_requested()) {
		b->apply(fn_prev_char);
		prof->char_dec_count++;
	}
}

auto char_incrementer_thread(std::stop_token stop, atom::val<model_b>* b, profiling* prof) -> void {
	while (!stop.stop_requested()) {
		b->apply(fn_next_char);
		prof->char_inc_count++;
	}
}

auto main(int, const char*[]) -> int {
	auto prof = profiling{};
	{
		// We create multiple vals of of the same <T> as they will be sharing
		// hazard pointer slots under the hood so we just want to check
		// everything is ok with that sharing.
		auto a0             = atom::val<model_a>{};
		auto a1             = atom::val<model_a>{};
		auto b0             = atom::val<model_b>{};
		auto b1             = atom::val<model_b>{};
		auto a0_counter_dec = std::jthread{counter_decrementer_thread, &a0, &prof};
		auto a0_counter_inc = std::jthread{counter_incrementer_thread, &a0, &prof};
		auto a0_counter_set = std::jthread{counter_setter_thread, &a0, &prof};
		auto a0_return_42   = std::jthread{return_42_thread, &a0, &prof};
		auto b0_char_dec    = std::jthread{char_decrementer_thread, &b0, &prof};
		auto b0_char_inc    = std::jthread{char_incrementer_thread, &b0, &prof};
		auto a1_counter_dec = std::jthread{counter_decrementer_thread, &a1, &prof};
		auto a1_counter_inc = std::jthread{counter_incrementer_thread, &a1, &prof};
		auto a1_counter_set = std::jthread{counter_setter_thread, &a1, &prof};
		auto a1_return_42   = std::jthread{return_42_thread, &a1, &prof};
		auto b1_char_dec    = std::jthread{char_decrementer_thread, &b1, &prof};
		auto b1_char_inc    = std::jthread{char_incrementer_thread, &b1, &prof};
		auto printer        = std::jthread{printer_thread, &a0, &a1, &b0, &b1, &prof};
		std::this_thread::sleep_for(RUN_DURATION);
	}
	std::cout << std::format(
		"\nProfiling results:\n"
		"print: {}\n"
		"counter dec: {}\n"
		"counter inc: {}\n"
		"counter set: {}\n"
		"counter ret: {}\n"
		"char dec: {}\n"
		"char inc: {}\n"
		"---------------------\n"
		"total: {}\n",
		prof.print_count,
		prof.counter_dec_count,
		prof.counter_inc_count,
		prof.counter_set_count,
		prof.counter_ret_count,
		prof.char_dec_count,
		prof.char_inc_count,
		prof.print_count + prof.counter_dec_count + prof.counter_inc_count + prof.counter_set_count + prof.counter_ret_count + prof.char_dec_count + prof.char_inc_count);
	return 0;
}
