#include <atom.hpp>
#include <format>
#include <iostream>

// This is just a smoke test against compile errors, crashes,
// assertion failures and memory leaks and also serves as a
// basic usage example.

static constexpr auto RUN_DURATION = std::chrono::seconds(10);

struct model {
	int counter = 0;
};

atom::val<model> g_model;

constexpr auto fn_decrement_counter = [](model x) { x.counter--; return x; };
constexpr auto fn_increment_counter = [](model x) { x.counter++; return x; };

// Shows how apply an action to the model and return a value.
constexpr auto fn_just_return_42 = [](model x) {
	// ...could modify the model here...
	return std::make_pair(x, 42);
};

auto fn_set_counter(int value) { return [value](model x) { x.counter = value; return x; }; }

auto print_counter_value(const model& v) -> void {
	std::cout << std::format("counter: {}\n", v.counter);
}

auto printer_thread(std::stop_token stop) -> void {
	while (!stop.stop_requested()) {
		const auto model = g_model.get();
		print_counter_value(model);
	}
}

auto decrementer_thread(std::stop_token stop) -> void {
	while (!stop.stop_requested()) {
		g_model.apply(fn_decrement_counter);
	}
}

auto incrementer_thread(std::stop_token stop) -> void {
	while (!stop.stop_requested()) {
		g_model.apply(fn_increment_counter);
	}
}

auto return_42_thread(std::stop_token stop) -> void {
	while (!stop.stop_requested()) {
		const auto value = g_model.apply_r(fn_just_return_42);
		assert (value == 42);
	}
}

auto setter_thread(std::stop_token stop) -> void {
	for (;;) {
		if (stop.stop_requested()) {
			break;
		}
		g_model.apply(fn_set_counter(0));
	}
}

auto main(int, const char*[]) -> int {
	auto decrementer = std::jthread{decrementer_thread};
	auto incrementer = std::jthread{incrementer_thread};
	auto return_42   = std::jthread{return_42_thread};
	auto setter      = std::jthread{setter_thread};
	auto printer     = std::jthread{printer_thread};
	std::this_thread::sleep_for(RUN_DURATION);
	return 0;
}
