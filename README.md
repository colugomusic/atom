# `atom::val<T>`

This is an experimental header-only C++ lock-free thread synchronization primitive inspired by Clojure Atoms.

The interface is very simple:

```c++
namespace atom {
template <typename T>
struct val {
	val(int gc_threshold = hazptr::DEFAULT_GC_THRESHOLD);
	~val();
	[[nodiscard]] auto get() const -> T;
	auto apply(auto fn) -> T;
	auto apply_r(auto fn) -> decltype(auto);
private:
	...
};
} // atom
```

- `get`: Returns the current value.
- `apply`: Applys `fn` to the value and returns the new value.
- `apply_r`: Applys `fn` to the value and returns some other value.

## Notes

- This entire API is **lock-free**.
- `get()` is also **realtime-safe**.
- `apply()` and `apply_r()` are not realtime-safe as they may occasionally allocate memory.
- I intended this to be used in a single-atom architecture where you have a single value representing the entire state of your system, so it's expected that you will only create a single `atom::val<T>`, but you can create more than one if you need to. There is a little bit of memory overhead required for each `val` of unique `T` that you instantiate.

## What does `apply()` do under the hood?

This is conceptually what happens:

1. Read the current value.
2. Apply `fn` to it.
3. Check if the value was overwritten by any other thread while `fn` was running. If it was, discard the changes and go back to step 1.
4. Set the value to the result of `fn`.

This means that `fn` may be called multiple times, so it should be pure.

## What is a single-atom architecture?

Please go and watch these videos. I promise they are interesting!

- [CppCon 2017: Juan Pedro Bolivar Puente “Postmodern immutable data structures”](https://www.youtube.com/watch?v=sPhpelUfu8Q)
- [CppCon 2018: Juan Pedro Bolivar Puente "The Most Valuable Values”](https://www.youtube.com/watch?v=_oBx_NbLghY)
- [CppCon 2021: Juan Pedro Bolivar Puente "Value-oriented Design in an Object-oriented System”](https://www.youtube.com/watch?v=67MmJSw4bxo)

You are now up to date!

## Usage example

```c++
struct model {
	int counter = 0;
};

atom::val<model> g_model;

constexpr auto fn_decrement_counter = [](model x) { x.counter--; return x; };
constexpr auto fn_increment_counter = [](model x) { x.counter++; return x; };
auto fn_set_counter(int value) { return [value](model x) { x.counter = value; return x; }; }

// Shows how to apply an action to the model and return a different value.
constexpr auto fn_just_return_42 = [](model x) {
	// ...could modify the model here...
	// Then we return a pair holding the new value of the
	// model, and the value we want to return.
	return std::make_pair(x, 42);
};

auto print_counter_value(const model& v) -> void {
	std::cout << std::format("counter: {}\n", v.counter);
}

auto printer_thread(std::stop_token stop) -> void {
	while (!stop.stop_requested()) {
		print_counter_value(g_model.get());
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
	while (!stop.stop_requested()) {
		g_model.apply(fn_set_counter(0));
	}
}

auto main(int, const char*[]) -> int {
    static constexpr auto RUN_DURATION = std::chrono::seconds(10);
	auto decrementer = std::jthread{decrementer_thread};
	auto incrementer = std::jthread{incrementer_thread};
	auto return_42   = std::jthread{return_42_thread};
	auto setter      = std::jthread{setter_thread};
	auto printer     = std::jthread{printer_thread};
	std::this_thread::sleep_for(RUN_DURATION);
	return 0;
}

```

## How does the memory reclamation work?

Wow thank you so much for asking. It works using hazard pointers. Every N calls to `apply()` or `apply_r()`, a garbage collection is run. By default N is 2000 but you can pass a different value to the `atom::val<T>` constructor.

My hazard pointer implementation is partly based on [Daniel Anderson's implementation](https://github.com/DanielLiamAnderson/atomic_shared_ptr/blob/master/include/parlay/details/hazard_pointers.hpp). I'm not sure that a general-purpose, non-intrusive hazard pointer implementation is possible, and we need non-intrusivity, so my implementation is NOT general-purpose and is specifically designed for `atom::val<T>`. In my implementation memory is not actually deleted to the free store during garbage collection, it's returned to a per-hazard-slot pool for re-use.
