#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef NDEBUG
    #define DA_ASSERT(x) assert(x)
#else
    #define DA_ASSERT(x) ((void) 0)
#endif

template <typename T>
class DynamicArray {
public:
    using value_type = T;
    using size_type = std::size_t;

    // ---- constructors ----
    constexpr DynamicArray() = default;

    explicit DynamicArray(size_type count) : data(count) {}

    DynamicArray(std::initializer_list<T> init) : data(init) {}

    // ---- size ----
    [[nodiscard]] constexpr size_type length() const noexcept {
        return data.size();
    }

    [[nodiscard]] constexpr bool isEmpty() const noexcept {
        return data.empty();
    }

    constexpr void clear() noexcept {
        data.clear();
    }

    void reserve(size_type capacity) {
        data.reserve(capacity);
    }

    // ---- element access ----
    constexpr T& operator[](size_type index) noexcept {
        DA_ASSERT(index < data.size());
        return data[index];
    }

    constexpr const T& operator[](size_type index) const noexcept {
        DA_ASSERT(index < data.size());
        return data[index];
    }

    constexpr T& first() {
        DA_ASSERT(!data.empty());
        return data.front();
    }

    constexpr const T& first() const {
        DA_ASSERT(!data.empty());
        return data.front();
    }

    constexpr T& last() {
        DA_ASSERT(!data.empty());
        return data.back();
    }

    constexpr const T& last() const {
        DA_ASSERT(!data.empty());
        return data.back();
    }

    // ---- mutation ----
    void push(const T& value) {
        data.push_back(value);
    }

    void push(T&& value) {
        data.push_back(std::move(value));
    }

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    T& pushNew(Args&&... args) {
        data.emplace_back(std::forward<Args>(args)...);
        return data.back();
    }

    T pop() {
        DA_ASSERT(!data.empty());
        T value = std::move(data.back());
        data.pop_back();
        return value;
    }

    void removeAt(size_type index) {
        DA_ASSERT(index < data.size());
        data.erase(data.begin() + index);
    }

    // O(1), order not preserved
    void swapRemoveAt(size_type index) {
        DA_ASSERT(index < data.size());
        data[index] = std::move(data.back());
        data.pop_back();
    }

    // ---- TS-style helpers ----
    [[nodiscard]] bool includes(const T& value) const {
        return std::find(data.begin(), data.end(), value) != data.end();
    }

    [[nodiscard]] int indexOf(const T& value) const {
        auto it = std::find(data.begin(), data.end(), value);
        return it == data.end()
            ? -1
            : static_cast<int>(std::distance(data.begin(), it));
    }

    template <typename Fn>
        requires std::invocable<Fn, T&>
    void forEach(Fn&& fn) {
        for (auto& v : data)
            fn(v);
    }

    template <typename Fn>
        requires std::invocable<Fn, const T&>
    void forEach(Fn&& fn) const {
        for (const auto& v : data)
            fn(v);
    }

    template <typename Fn>
        requires std::invocable<Fn, const T&>
    auto map(Fn&& fn) const
        -> DynamicArray<std::invoke_result_t<Fn, const T&>> {
        using R = std::invoke_result_t<Fn, const T&>;

        DynamicArray<R> result;
        result.reserve(data.size());

        for (const auto& v : data)
            result.push(fn(v));

        return result;
    }

    template <typename Fn>
        requires std::predicate<Fn, const T&>
    DynamicArray filter(Fn&& fn) const {
        DynamicArray result;

        for (const auto& v : data) {
            if (fn(v))
                result.push(v);
        }

        return result;
    }

    // ---- iteration ----
    auto begin() noexcept { return data.begin(); }
    auto end() noexcept { return data.end(); }

    auto begin() const noexcept { return data.begin(); }
    auto end() const noexcept { return data.end(); }

    // ---- STL interop ----
    std::vector<T>& stdVector() noexcept { return data; }
    const std::vector<T>& stdVector() const noexcept { return data; }

private:
    std::vector<T> data;
};
