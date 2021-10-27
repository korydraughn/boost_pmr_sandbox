#include <cstddef>
#include <cassert>
#include <iostream>
#include <array>
#include <stdexcept>
#include <chrono>
#include <algorithm>
#include <random>
#include <type_traits>
#include <vector>
#include <iomanip>
#include <memory>
#include <vector>
#include <iterator>

#include <boost/container/pmr/memory_resource.hpp>
#include <boost/container/pmr/unsynchronized_pool_resource.hpp>
#include <boost/container/pmr/vector.hpp>
#include <boost/container/pmr/string.hpp>

namespace pmr = boost::container::pmr;

class capped_memory_pool
    : public pmr::memory_resource
{
public:
    explicit capped_memory_pool(std::size_t _max_size)
        : pmr::memory_resource{}
        , max_size_(_max_size)
        , allocated_{}
    {
    }

    std::size_t allocated() const noexcept
    {
        return allocated_;
    }

protected:
    void* do_allocate(std::size_t _bytes, std::size_t) override
    {
        if (allocated_ + _bytes >= max_size_)
            throw std::bad_alloc{};

        if (auto* p = std::malloc(_bytes); p) {
            allocated_ += _bytes;
            return p;
        }

        throw std::bad_alloc{};
    }

    void do_deallocate(void* _p, std::size_t _bytes, std::size_t) override
    {
        std::free(_p);
        allocated_ -= _bytes;
    }

    bool do_is_equal(const pmr::memory_resource& _other) const noexcept override
    {
        return this == &_other;
    }

private:
    std::size_t max_size_;
    std::size_t allocated_;
};

template <typename ByteRep>
class fixed_buffer_resource
    : public boost::container::pmr::memory_resource
{
public:
    static_assert(std::is_same_v<ByteRep, char> || std::is_same_v<ByteRep, std::byte>);

    fixed_buffer_resource(ByteRep* _buffer, std::size_t _buffer_size)
        : boost::container::pmr::memory_resource{}
        , buffer_{_buffer}
        , buffer_size_{_buffer_size}
        , allocated_{}
        , overhead_{}
        , headers_{}
        , freelist_{}
    {
        assert(_buffer);
        assert(_buffer_size > 0);

        headers_ = reinterpret_cast<header*>(buffer_);
        headers_->size = buffer_size_ - sizeof(header);
        headers_->prev = nullptr;
        headers_->next = nullptr;
        *address_of_used_flag(headers_) = false;

        overhead_ = sizeof(header);

        freelist_.reserve(100);
        freelist_.push_back(headers_);
    } // fixed_buffer_resource

    fixed_buffer_resource(const fixed_buffer_resource&) = delete;
    auto operator=(const fixed_buffer_resource&) -> fixed_buffer_resource& = delete;

    ~fixed_buffer_resource() = default;

    auto allocated() const noexcept -> std::size_t
    {
        return allocated_;
    } // allocated

    auto allocation_overhead() const noexcept -> std::size_t
    {
        return overhead_;
    } // allocation_overhead

    auto print() -> void
    {
        std::size_t i = 0;

        for (auto* h = headers_; h; h = h->next) {
            std::cout << std::setw(3) << std::right << i
                      << ". header [" << (void*) h
                      << "]: {prev=" << std::setw(14) << (void*) h->prev
                      << ", next=" << std::setw(14) << (void*) h->next
                      << ", used=" << is_data_in_use(h)
                      << ", data=" << std::setw(14) << (void*) (reinterpret_cast<ByteRep*>(h) + sizeof(header))
                      << ", size=" << h->size
                      << "}\n";
            ++i;
        }
    }

protected:
    struct header
    {
        std::size_t size;   // Size of the memory block (excluding header).
        header* prev;       // Pointer to the previous header block.
        header* next;       // Pointer to the next header block.
    }; // struct header

    static constexpr std::size_t management_block_size = sizeof(header) + 1;

    auto do_allocate(std::size_t _bytes, std::size_t _alignment) -> void* override
    {
        for (auto* h : freelist_) {
            if (auto* p = handle_block(_bytes, _alignment, h); p) {
                remove_header_from_freelist(h);
                return p;
            }
        }

        for (auto* h = headers_; h; h = h->next) {
            if (auto* p = handle_block(_bytes, _alignment, h); p) {
                remove_header_from_freelist(h);
                return p;
            }
        }

        throw std::bad_alloc{};
    } // do_allocate

    auto do_deallocate(void* _p, std::size_t _bytes, std::size_t _alignment) -> void override
    {
        auto* h = reinterpret_cast<header*>(static_cast<ByteRep*>(_p) - management_block_size);
        auto* used = address_of_used_flag(h);

        assert(*used);
        assert(h->size == _bytes);

        *used = false;

        freelist_.push_back(h);

        coalesce_with_next_node(h);
        coalesce_with_next_node(h->prev);

        allocated_ -= _bytes;
    } // do_deallocate

    auto do_is_equal(const boost::container::pmr::memory_resource& _other) const noexcept -> bool override
    {
        return this == &_other;
    } // do_is_equal

    auto address_of_used_flag(header* _h) const noexcept -> bool*
    {
        return static_cast<bool*>(static_cast<void*>(reinterpret_cast<ByteRep*>(_h) + sizeof(header)));
    } // address_of_used_flag

    auto address_of_data_segment(header* _h) const noexcept -> ByteRep*
    {
        return reinterpret_cast<ByteRep*>(_h) + management_block_size;
    } // address_of_data_segment

    auto is_data_in_use(header* _h) const noexcept -> bool
    {
        return *address_of_used_flag(_h);
    } // is_data_in_use

private:
    auto handle_block(std::size_t _bytes, std::size_t _alignment, header* _h) -> void*
    {
        if (is_data_in_use(_h)) {
            return nullptr;
        }

        // Return the block if it matches the requested number of bytes exactly.
        if (_bytes == _h->size) {
            *address_of_used_flag(_h) = true;
            allocated_ += _bytes;
            return address_of_data_segment(_h);
        }

        // Split the block.
        // In order to split the header's memory, the header's segment must be large
        // enough to hold a new block (i.e. header + data).
        if (management_block_size + _bytes < _h->size) {
            // The unused memory is located right after the header.
            auto* data = address_of_data_segment(_h);
#if 0
            void* vdata = data;
            auto* aligned_data = std::align(_alignment, _bytes, vdata, _h->size);
            //std::cout << __func__ << ":" << __LINE__ << " - data address         = " << (void*) data << '\n';
            //std::cout << __func__ << ":" << __LINE__ << " - aligned data address = " << (void*) aligned_data << '\n';
            
            if (!aligned_data) {
                continue;
            }
#endif
            // Insert a new header after the data section.
            // This header manages memory that has not been allocated yet.
            auto* new_header = reinterpret_cast<header*>(data + _bytes);
            new_header->size = _h->size - _bytes - management_block_size;
            new_header->prev = _h;
            new_header->next = _h->next;
            *address_of_used_flag(new_header) = false;

            freelist_.push_back(new_header);

            // Update the links of the header's adjacent node.
            if (auto* next_header = _h->next; next_header) {
                next_header->prev = new_header;
            }

            // Adjust the current header's size and mark it as used.
            _h->size = _bytes;
            _h->next = new_header;
            *address_of_used_flag(_h) = true;

            allocated_ += _bytes;
            overhead_ += management_block_size;

            return data;
        }

        return nullptr;
    } // find_unused_block

    auto coalesce_with_next_node(header* _h) -> void
    {
        if (!_h || is_data_in_use(_h)) {
            return;
        }

        auto* header_to_remove = _h->next;

        if (header_to_remove && !is_data_in_use(header_to_remove)) {
            _h->size += header_to_remove->size;
            _h->next = header_to_remove->next;

            if (auto* new_next_node = header_to_remove->next; new_next_node) {
                new_next_node->prev = _h;
            }

            overhead_ -= management_block_size;
            remove_header_from_freelist(header_to_remove);
        }
    } // coalesce_with_next_node

    auto remove_header_from_freelist(header* _h) -> void
    {
        const auto end = std::end(freelist_);

        if (const auto iter = std::find(std::begin(freelist_), end, _h); iter != end) {
            freelist_.erase(iter);
        }
    }

    ByteRep* buffer_;
    std::size_t buffer_size_;
    std::size_t allocated_;
    std::size_t overhead_;
    header* headers_;
    std::vector<header*> freelist_;
}; // fixed_buffer_resource

std::string random_string(std::size_t length)
{
    const auto randchar = []() -> char
    {
        const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[rand() % max_index];
    };
    
    std::string str(length, 0);
    std::generate_n(str.begin(), length, randchar);
    
    return str;
}

template <typename Allocator>
auto do_test(Allocator& _allocator, std::size_t n_strings, std::size_t _string_length) -> void
{
    std::cout << "Running Test [string count=" << n_strings
              << ", string length=" << std::setw(2) << std::right << _string_length << "]: ";

#ifdef SHOW_OVERHEAD
    if constexpr (std::is_same_v<Allocator, fixed_buffer_resource<std::byte>>) {
        std::cout << "(on test start) total memory allocated: " << _allocator.allocated() << '\n';
        std::cout << "(on test start) total memory overhead : " << _allocator.allocation_overhead() << '\n';
    }
#endif

    pmr::vector<pmr::string> strings{&_allocator};
    
    const auto start = std::chrono::system_clock::now();
    
    for (std::size_t i = 0; i < n_strings; ++i) {
        strings.emplace_back(random_string(_string_length).data());
    }
    
    const auto elapsed = std::chrono::system_clock::now() - start;
    const auto t = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    std::cout << t.count() << "ms\n";

#ifdef SHOW_OVERHEAD
    if constexpr (std::is_same_v<Allocator, capped_memory_pool> ||
                  std::is_same_v<Allocator, fixed_buffer_resource<std::byte>>)
    {
        std::cout << "total memory allocated: " << _allocator.allocated() << '\n';
    }

    if constexpr (std::is_same_v<Allocator, fixed_buffer_resource<std::byte>>) {
        std::cout << "(on test complete) total memory overhead: " << _allocator.allocation_overhead() << '\n';
    }
#endif
}

int main(int _argc, char** _argv)
{
    constexpr auto strings_to_allocate = 75'000;

    try {
        constexpr std::size_t max_size = 50'000'000;

        if (_argc != 1) {
            std::cout << "================================\n";
            std::cout << "testing: new_delete_resource\n";
            std::cout << "------------------------------\n";
            auto* ndr = pmr::new_delete_resource();
            do_test(*ndr, strings_to_allocate, 8);
            do_test(*ndr, strings_to_allocate, 16);
            do_test(*ndr, strings_to_allocate, 32);
            do_test(*ndr, strings_to_allocate, 64);
            do_test(*ndr, strings_to_allocate, 191);
            std::cout << '\n';
        }
        
        std::cout << "================================\n";
        std::cout << "testing: capped_memory_pool\n";
        std::cout << "-----------------------------\n";
        capped_memory_pool cmp{max_size};
        do_test(cmp, strings_to_allocate, 8);
        do_test(cmp, strings_to_allocate, 16);
        do_test(cmp, strings_to_allocate, 32);
        do_test(cmp, strings_to_allocate, 64);
        do_test(cmp, strings_to_allocate, 191);
        std::cout << '\n';
        
        std::cout << "================================\n";
        std::cout << "testing: fixed_buffer_resource (w/o unsynchronized_pool_resource)\n";
        std::cout << "--------------------------------\n";
        std::vector<std::byte> buffer(max_size);
        fixed_buffer_resource fbr{buffer.data(), max_size};
        do_test(fbr, strings_to_allocate, 8);
        do_test(fbr, strings_to_allocate, 16);
        do_test(fbr, strings_to_allocate, 32);
        do_test(fbr, strings_to_allocate, 64);
        do_test(fbr, strings_to_allocate, 191);
        std::cout << '\n';

        std::cout << "\n================================\n";
        std::cout << "testing: fixed_buffer_resource (w/ unsynchronized_pool_resource)\n";
        std::cout << "--------------------------------\n";
        pmr::unsynchronized_pool_resource upr{&fbr};
        do_test(upr, strings_to_allocate, 8);
        do_test(upr, strings_to_allocate, 16);
        do_test(upr, strings_to_allocate, 32);
        do_test(upr, strings_to_allocate, 64);
        do_test(upr, strings_to_allocate, 191);
    }
    catch (const std::exception& e) {
        std::cout << e.what() << '\n';
    }

    return 0;
}

