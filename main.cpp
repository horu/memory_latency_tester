#include  <cstdint>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <chrono>
#include <string>
#include <iomanip>
#include <array>
#include <atomic>
#include <thread>
#include <functional>
#include <random>

#include <unistd.h>
#include <sys/syscall.h>

using Type = uint64_t;
size_t uint_size = 8;

unsigned debug = 0;
Type count_jumps = 10000000;
size_t thread_count = 1;

size_t get_time_ms()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}


template<class T, class Uid, class Gen>
void random_swap(T& v, typename T::value_type& i, Uid& uid, Gen& gen)
{
	auto r = uid(gen);
	auto& to_swap = v[r];
	std::swap(i, to_swap);
}

template<class T>
size_t random_generator(T& v)
{
	if (debug) std::cerr << "start random gen\n";
	auto start = get_time_ms();
	auto vector_size = v.size();
	auto tid = syscall(SYS_gettid);
	if (debug) std::cerr << "tid " << tid <<"\n";
	std::mt19937 gen(std::time(0) + tid);
	std::uniform_int_distribution<> uid(0, vector_size-1);
	for (size_t i = 0; i < vector_size; ++i)
	{
		v[i] = i;
	}
	if (debug) std::cerr << "create vector " << get_time_ms()-start << "\n";
	for (auto& i : v)
	{
		random_swap(v, i, uid, gen);
	}
	if (debug) std::cerr << "first random gen complited " << get_time_ms()-start << "\n";

	bool error = true;
	while (error)
	{
		error = false;
		for (size_t i = 0; i < vector_size; ++i)
		{
			auto& value = v[i];
			if (value == i)
			{
				if (debug) std::cerr << "error :" << static_cast<uint64_t>(value) << "\n";
				random_swap(v, value, uid, gen);
				error = true;
				break;
			}
		}
	}
	auto stop = get_time_ms();
	if (debug) std::cerr << "stop random gen " << get_time_ms()-start << "\n";
	if (debug & 2) for (auto& i : v)
	{
		std::cerr << i << "\n";
	}
	return stop-start;
}

class SubVector
{
public:
	SubVector(size_t cur_vector_size):
		cur_vector_size_(cur_vector_size)
	{
		random_generator(sub_v_);
			if (debug & 8) for (auto& i : sub_v_)
			{
				std::cerr << static_cast<uint64_t>(i) << "\n";
			}
	}

	void random(Type& i)
	{
		it_index();
		auto random_delta_i = sub_v_[current_random_index_];
		i += random_delta_i;
		if (i >= cur_vector_size_)
		{
			i -= random_delta_i;
		}
	}

	void it_index()
	{
		current_random_index_++;
		if (current_random_index_ >= sub_v_.size())
		{
			current_random_index_ = 0;
		}
	}


private:

	size_t current_random_index_ = 0;
	size_t cur_vector_size_;
	std::array<uint8_t,128> sub_v_;
};


class ReadyHandle
{
public:
	ReadyHandle(unsigned thread_count):
		ready_handle_(thread_count)
	{}

	void wait_start()
	{
		ready_handle_.fetch_sub(1);
		while (ready_handle_.load()) {}
	}

private:
	std::atomic<unsigned> ready_handle_;
};


void test(std::vector<Type>& v, Type& result, std::atomic<size_t>& time, ReadyHandle& ready_handle)
{
	auto cur_vector_size = v.size();
	auto sub_v = SubVector(cur_vector_size);
	Type i = 0;
	sub_v.random(i);
	Type cur_jumps = count_jumps;
	ready_handle.wait_start();
	auto start = get_time_ms();
	if (debug) std::cout << "start test with:" << i << "\n";
	while (--cur_jumps)
	{
//		if (debug & 4) std::cout << "test:" << i << "\n";
		sub_v.random(i);
		i = v[i];
	}
	if (debug) std::cout << "test time:" << time.load() << "\n";
	auto stop = get_time_ms();
	result = i;
	time.fetch_add(stop - start);
}

int main(int argc, char *argv[])
{
	size_t max_vector_bytes = 50000000000;
	size_t min_vector_bytes = 1000;

	for (int i = 1; i < argc; ++i)
	{
		std::string option = argv[i];
		if (option == "debug")
		{
			++i;
			debug = std::stoul(argv[i]);
		}
		else if (option == "jumps")
		{
			++i;
			count_jumps = std::stoul(argv[i]);
		}
		else if (option == "max")
		{
			++i;
			max_vector_bytes = std::stoul(argv[i]);
		}
		else if (option == "min")
		{
			++i;
			min_vector_bytes = std::stoul(argv[i]);
		}
		else if (option == "threads")
		{
			++i;
			thread_count = std::stoul(argv[i]);
		}
		else
		{
			std::cout << "invalid option " << option << std::endl;
			return 1;
		}
	}
	std::cout << "usage: jumps max min debug\n\n";

	std::cout << " max_vector_bytes:" << max_vector_bytes
			  << " count_jumps:" << count_jumps
			  << " debug:" << debug
			  << "\n";

	Type i = 0;

	std::cout << "start \n";
	for (size_t cur_vector_bytes = min_vector_bytes; cur_vector_bytes < max_vector_bytes; cur_vector_bytes *= 2)
	{
		size_t avg_timeout = 0;
		size_t cur_vector_size = cur_vector_bytes/uint_size;
		std::vector<Type> v(cur_vector_size);

		for (size_t it = 1; it <= 1; it++)
		{
			auto gen_time = random_generator(v);
			std::atomic<size_t> sum_t_timeout(0);
			std::vector<std::thread> threads;
			ReadyHandle ready_handle(thread_count);
			for (unsigned t = 0; t < thread_count; t++)
			{
				threads.emplace_back(test, std::ref(v), std::ref(i), std::ref(sum_t_timeout), std::ref(ready_handle));
			}

			for (auto& t : threads)
			{
				t.join();
			}
			size_t timeout = sum_t_timeout / threads.size();
			avg_timeout += timeout;
			auto sw = 10;
			std::cout << "cur_vector_bytes, kb: " << std::setw( 10 ) <<  cur_vector_bytes/1000
					  << "| latency, ns :"         << std::setw( 6 ) << double(timeout) * 1000000 / count_jumps
					  << "| time, ms :"    		  << std::setw( 7 ) << timeout
					  << "| all time :"     		  << std::setw( 7 ) << avg_timeout/it
					  << "| gen time, ms :"     		  << std::setw( 7 ) << gen_time
					  << "\n";
		}
	}

	return static_cast<int>(i);
}
