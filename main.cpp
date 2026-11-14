// Компілятор: MinGW (стандарт C++20)
#include <coroutine>
#include <exception>
#include <format>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace std;

const int MIN_VALUE = 1;
const int MAX_VALUE = 256;
const int MAX_COUNT = 10000;

class Job
{
public:
    struct promise_type;
    using handle = coroutine_handle<promise_type>;

    void start() { coro.resume(); }
    coroutine_handle<> get_handle() const { return coro; }

    struct promise_type
    {
        auto get_return_object() { return Job{handle::from_promise(*this)}; }
        auto initial_suspend() { return suspend_always{}; }
        auto final_suspend() noexcept { return suspend_always{}; }
        void return_void() {}
        void unhandled_exception() { terminate(); }
    };
    Job(Job const &) = delete;
    Job &operator=(Job const &) = delete;
    Job(Job &&other) : coro(other.coro) { other.coro = nullptr; }
    ~Job()
    {
        if (coro)
            coro.destroy();
    }

private:
    Job(handle h) : coro(h) {}
    handle coro;
};

struct Channel
{
    int value = 0;
    coroutine_handle<> sender;
    coroutine_handle<> receiver;
};

class TransferAwaiter
{
public:
    TransferAwaiter(int number, Channel &channel) : number(number), channel(channel) {}
    bool await_ready() const noexcept { return false; }
    coroutine_handle<> await_suspend(coroutine_handle<> h) noexcept;
    void await_resume() noexcept {}

private:
    int number;
    Channel &channel;
};

class ReturnAwaiter
{
public:
    ReturnAwaiter(const Channel &channel) : channel(channel) {}
    bool await_ready() const noexcept { return false; }
    coroutine_handle<> await_suspend(coroutine_handle<> h) noexcept;
    void await_resume() noexcept {}

private:
    const Channel &channel;
};

coroutine_handle<> TransferAwaiter::await_suspend(coroutine_handle<> h) noexcept
{
    if (number % 2 == 0)
    {
        channel.value = number;
        channel.sender = h;
        return channel.receiver;
    }
    return h;
}

coroutine_handle<> ReturnAwaiter::await_suspend(coroutine_handle<>) noexcept
{
    return channel.sender;
}

Job random_numbers(int count, shared_ptr<Channel> channel)
{
    random_device r;
    mt19937 e(r());
    uniform_int_distribution<int> d(MIN_VALUE, MAX_VALUE);
    for (int i = 1; i <= count; ++i)
    {
        int number = d(e);
        cout << format("[{:>5}] generated: {:>3}", i, number) << endl;
        co_await TransferAwaiter{number, *channel};
    }
}

Job print_even(shared_ptr<Channel> channel)
{
    while (true)
    {
        cout << format("        A printed: {:>3}", channel->value) << endl;
        co_await ReturnAwaiter{*channel};
    }
}

string read_line()
{
    string line;
    if (!getline(cin, line))
        throw runtime_error("input ended unexpectedly");
    return line;
}

bool parse_int(const string &line, int &value)
{
    istringstream in(line);
    return (in >> value) && (in >> ws).eof();
}

int read_count()
{
    while (true)
    {
        cout << format("Count of numbers [1..{}]: ", MAX_COUNT);
        string line = read_line();
        int count = 0;
        if (!parse_int(line, count))
            cout << format("Error: \"{}\" is not a valid integer", line) << endl;
        else if (count < 1 || count > MAX_COUNT)
            cout << format("Error: count must be in [1, {}]", MAX_COUNT) << endl;
        else
            return count;
    }
}

void demo_even_transfer(int count)
{
    cout << format("Generating {} random numbers in [{}, {}]", count, MIN_VALUE, MAX_VALUE) << endl;
    cout << "Even numbers go to coroutine A, odd numbers resume the generator" << endl
         << endl;

    auto channel = make_shared<Channel>();
    auto printer = print_even(channel);
    channel->receiver = printer.get_handle();
    auto generator = random_numbers(count, channel);
    generator.start();

    cout << endl
         << "Done." << endl;
}

int main()
{
    try
    {
        demo_even_transfer(read_count());
    }
    catch (const exception &error)
    {
        cerr << format("Error: {}", error.what()) << endl;
        return 1;
    }
    return 0;
}
