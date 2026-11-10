// Компілятор: MinGW (стандарт C++20)
#include <charconv>
#include <coroutine>
#include <cstdint>
#include <format>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <windows.h>

struct Range
{
    long long min;
    long long max;

    bool contains(long long value) const
    {
        return value >= min && value <= max;
    }
};

constexpr int MinNumber = 1;
constexpr int MaxNumber = 256;
constexpr Range CountRange{1, 10000};
constexpr Range SeedRange{0, 4294967295LL};

struct Settings
{
    int count;
    std::uint32_t seed;
};

class Coroutine
{
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type {
        Coroutine get_return_object()
        {
            return Coroutine{Handle::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { throw; }
    };

    explicit Coroutine(Handle handle) : handle_(handle) {}

    Coroutine(Coroutine&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Coroutine(const Coroutine&) = delete;
    Coroutine& operator=(const Coroutine&) = delete;
    Coroutine& operator=(Coroutine&&) = delete;

    ~Coroutine()
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    void resume() const
    {
        handle_.resume();
    }

    std::coroutine_handle<> handle() const
    {
        return handle_;
    }

private:
    Handle handle_;
};

struct Channel {
    int value = 0;
    std::coroutine_handle<> sender;
};

class TransferOnEven
{
public:
    TransferOnEven(int number, Channel& channel, std::coroutine_handle<> receiver)
        : number_(number), channel_(channel), receiver_(receiver) {}

    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> self) noexcept {
        if (isEven())
        {
            return passToReceiver(self);
        }
        return self;
    }

    void await_resume() const noexcept {}

private:
    bool isEven() const noexcept
    {
        return number_ % 2 == 0;
    }

    std::coroutine_handle<> passToReceiver(std::coroutine_handle<> self) noexcept
    {
        channel_.value = number_;
        channel_.sender = self;
        return receiver_;
    }

    int number_;
    Channel& channel_;
    std::coroutine_handle<> receiver_;
};

class ReturnToSender {
public:
    explicit ReturnToSender(const Channel& channel) : channel_(channel) {}

    bool await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept
    {
        return channel_.sender;
    }

    void await_resume() const noexcept {}

private:
    const Channel& channel_;
};

void printGenerated(int index, int number)
{
    std::cout << std::format("[{:>5}] Генератор: {:>3}\n", index, number);
}

void printReceived(int number)
{
    std::cout << std::format("              → A: {:>3}\n", number);
}

Coroutine generateNumbers(Settings settings, Channel& channel, std::coroutine_handle<> receiver) {
    std::mt19937 engine(settings.seed);
    std::uniform_int_distribution<int> distribution(MinNumber, MaxNumber);

    for (int index = 1; index <= settings.count; ++index) {
        const int number = distribution(engine);
        printGenerated(index, number);
        co_await TransferOnEven{number, channel, receiver};
    }
}

Coroutine printEvenNumbers(const Channel& channel)
{
    while (true) {
        printReceived(channel.value);
        co_await ReturnToSender{channel};
    }
}

std::string_view trim(std::string_view text)
{
    constexpr std::string_view whitespace = " \t\r\n";
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

std::optional<long long> parseInteger(std::string_view text)
{
    long long value = 0;
    const char* end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc{} || stop != end) {
        return std::nullopt;
    }
    return value;
}

std::string readLine(std::string_view prompt)
{
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) {
        throw std::runtime_error("ввід завершився раніше, ніж було отримано всі дані");
    }
    return line;
}

void reportInputError(std::string_view message)
{
    std::cerr << std::format("  Помилка: {}. Спробуйте ще раз.\n", message);
}

std::optional<long long> askOptionalNumber(std::string_view label, Range range)
{
    const std::string prompt = std::format("{} [{}..{}]: ", label, range.min, range.max);
    while (true) {
        const std::string line = readLine(prompt);
        const std::string_view text = trim(line);
        if (text.empty()) {
            return std::nullopt;
        }
        const std::optional<long long> value = parseInteger(text);
        if (!value) {
            reportInputError(std::format("\"{}\" не є коректним цілим числом", text));
        } else if (!range.contains(*value)) {
            reportInputError(std::format("число має бути в межах [{}, {}]", range.min, range.max));
        } else {
            return value;
        }
    }
}

long long askRequiredNumber(std::string_view label, Range range)
{
    while (true) {
        const std::optional<long long> value = askOptionalNumber(label, range);
        if (value) {
            return *value;
        }
        reportInputError("значення не може бути порожнім");
    }
}

std::uint32_t resolveSeed(std::optional<long long> seed)
{
    if (seed) {
        return static_cast<std::uint32_t>(*seed);
    }
    return std::random_device{}();
}

Settings readSettings()
{
    const long long count = askRequiredNumber("Кількість чисел", CountRange);
    const std::optional<long long> seed = askOptionalNumber("Seed (Enter — випадковий)", SeedRange);
    return Settings{static_cast<int>(count), resolveSeed(seed)};
}

void printHeader(const Settings& settings)
{
    std::cout << std::format("\nГенерація {} чисел з проміжку [{}, {}], seed = {}\n",
                             settings.count, MinNumber, MaxNumber, settings.seed);
    std::cout << "Парні числа передаються сопрограмі A, на непарних генератор продовжує роботу.\n\n";
}

void runDemo(const Settings& settings)
{
    printHeader(settings);
    Channel channel;
    const Coroutine printer = printEvenNumbers(channel);
    const Coroutine generator = generateNumbers(settings, channel, printer.handle());
    generator.resume();
    std::cout << "\nГенерацію завершено.\n";
}

void setupConsole()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

int main()
{
    setupConsole();

    try {
        runDemo(readSettings());
    }
    catch (const std::exception& error) {
        std::cerr << std::format("\nПомилка: {}\n", error.what());
        return 1;
    }
    return 0;
}
