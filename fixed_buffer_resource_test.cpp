#include <cstddef>
#include <cassert>
#include <iostream>
#include <array>
#include <stdexcept>
#include <chrono>
#include <algorithm>
#include <random>
#include <memory>

#include <boost/container/pmr/memory_resource.hpp>
#include <boost/container/pmr/unsynchronized_pool_resource.hpp>
#include <boost/container/pmr/vector.hpp>
#include <boost/container/pmr/string.hpp>
#include <boost/align/is_aligned.hpp>

#include <fmt/format.h>

namespace pmr = boost::container::pmr;

class capped_memory_pool
    : public pmr::memory_resource
{
public:
    explicit capped_memory_pool(std::int64_t _max_size)
        : pmr::memory_resource{}
        , max_size_(_max_size)
        , allocated_{}
    {
        if (_max_size <= 0) {
            throw std::runtime_error{"invalid value for max size"};
        }
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

        auto* p = std::malloc(_bytes);
        allocated_ += _bytes;

        return p;
    }

    void do_deallocate(void* _p, std::size_t _bytes, std::size_t) override
    {
        std::free(_p);
        allocated_ -= _bytes;
    }

    bool do_is_equal(const pmr::memory_resource&) const noexcept override
    {
        return true;
    }

private:
    std::size_t max_size_;
    std::size_t allocated_;
};

//#define IRODS_FIXED_BUFFER_RESOURCE_ENABLE_DEBUG

/// A namespace containing components meant to be used with Boost.Container's PMR library.
namespace irods::experimental::pmr
{
    /// A \p fixed_buffer_resource is a special purpose memory resource class template that
    /// allocates memory from the buffer given on construction. It allows applications to
    /// enforce a cap on the amount of memory available to components.
    ///
    /// This class implements a first-fit scheme and is NOT thread-safe.
    ///
    /// \tparam ByteRep The memory representation for the underlying buffer. Must be \p char
    ///                 or \p std::byte.
    ///
    /// \since 4.2.11
    template <typename ByteRep>
    class fixed_buffer_resource
        : public boost::container::pmr::memory_resource
    {
    public:
        static_assert(std::is_same_v<ByteRep, char> ||
                      std::is_same_v<ByteRep, unsigned char> ||
                      std::is_same_v<ByteRep, std::uint8_t> ||
                      std::is_same_v<ByteRep, std::byte>);

        /// Constructs a \p fixed_buffer_resource using the given buffer as the allocation
        /// source.
        ///
        /// \param[in] _buffer      The buffer that will be used for allocations.
        /// \param[in] _buffer_size The size of the buffer in bytes.
        ///
        /// \throws std::invalid_argument If any of the incoming constructor arguments do not
        ///                               satisfy construction requirements.
        ///
        /// \since 4.2.11
        fixed_buffer_resource(ByteRep* _buffer, std::int64_t _buffer_size)
            : boost::container::pmr::memory_resource{}
            , buffer_{_buffer}
            , buffer_size_(_buffer_size)
            , allocated_{}
            , overhead_{}
            , headers_{}
        {
            if (!_buffer || _buffer_size <= 0) {
                const auto* msg_fmt = "fixed_buffer_resource: invalid constructor arguments "
                                      "[buffer={}, size={}].";
                const auto msg = fmt::format(msg_fmt, fmt::ptr(_buffer), _buffer_size);
                throw std::invalid_argument{msg_fmt};
            }

            std::size_t space_left = buffer_size_;

            // Make sure the buffer is aligned for the header type.
            if (!std::align(alignof(header), sizeof(header), buffer_, space_left)) {
                throw std::runtime_error{"fixed_buffer_resource: internal memory alignment error. "};
            }

            headers_ = new (buffer_) header;
            headers_->size = space_left - sizeof(header);
            headers_->prev = nullptr;
            headers_->next = nullptr;
            headers_->used = false;

            // If "buffer_"'s alignment was adjusted, "buffer_size_" will be decreased
            // by the number of bytes used for alignment. We need to make sure that the
            // value of overhead reflects that.
            //overhead_ = (sizeof(header) + _buffer_size - buffer_size_);
            //overhead_ = calculate_overhead(headers_);
        } // fixed_buffer_resource

        fixed_buffer_resource(const fixed_buffer_resource&) = delete;
        auto operator=(const fixed_buffer_resource&) -> fixed_buffer_resource& = delete;

        ~fixed_buffer_resource() = default;

        /// Returns the number of bytes used by the client.
        ///
        /// The value returned does not include memory used for tracking allocations.
        ///
        /// \return An unsigned integral type.
        ///
        /// \since 4.2.11
        auto allocated() const noexcept -> std::size_t
        {
            return allocated_;
        } // allocated

        /// Returns the number of bytes used for tracking allocations.
        ///
        /// \return An unsigned integral type.
        ///
        /// \since 4.2.11
        auto allocation_overhead() const noexcept -> std::size_t
        {
#if 0
            std::size_t sum = 0;

            for (auto* h = headers_; h; h = h->next) {
                const auto overhead = calculate_overhead(h);
                sum += overhead;
                fmt::print("overhead for header [{}] = {}, total = {}\n", fmt::ptr(h), overhead, sum);
            }

            print(std::cout);

            return sum;
#else
            return overhead_;
#endif
        } // allocation_overhead

        /// Writes the state of the allocation table to the output stream.
        ///
        /// \since 4.2.11
        auto print(std::ostream& _os) const -> void
        {
            std::size_t i = 0;

            for (auto* h = headers_; h; h = h->next) {
                _os << fmt::format("{:>3}. Header Info [{}]: {{previous={:14}, next={:14}, used={:>5}, data={:14}, data_size={}}}\n",
                                  i,
                                  fmt::ptr(h),
                                  fmt::ptr(h->prev),
                                  fmt::ptr(h->next),
                                  h->used,
                                  fmt::ptr(address_of_data_segment(h)),
                                  h->size);
                ++i;
            }
        } // print

    protected:
        auto do_allocate(std::size_t _bytes, std::size_t _alignment) -> void* override
        {
            for (auto* h = headers_; h; h = h->next) {
                if (auto* p = allocate_block(_bytes, _alignment, h); p) {
#ifdef IRODS_FIXED_BUFFER_RESOURCE_ENABLE_DEBUG
                    std::cout << fmt::format("allocated [{}] bytes at address [{}]\n", _bytes, p);
                    print(std::cout); std::cout << '\n';
#endif
                    return p;
                }
            }

            throw std::bad_alloc{};
        } // do_allocate

        auto do_deallocate(void* _p, std::size_t _bytes, std::size_t _alignment) -> void override
        {
#ifdef IRODS_FIXED_BUFFER_RESOURCE_ENABLE_DEBUG
            fmt::print("deallocating [{}] bytes at address [{}] ...\n", _bytes, _p);
            print(std::cout); std::cout << '\n';
#endif
            void* data = *(static_cast<void**>(_p) - 1);
#ifdef IRODS_FIXED_BUFFER_RESOURCE_ENABLE_DEBUG
            fmt::print("unaligned data pointer = [{}]\n", fmt::ptr(data));
#endif
            auto* h = reinterpret_cast<header*>(static_cast<ByteRep*>(data) - sizeof(header));

            assert(h != nullptr);
            assert(h->size == _bytes);

            h->used = false;

            coalesce_with_next_unused_block(h);
            coalesce_with_next_unused_block(h->prev);

            allocated_ -= _bytes;
#ifdef IRODS_FIXED_BUFFER_RESOURCE_ENABLE_DEBUG
            fmt::print("deallocated [{}] bytes at address [{}]\n", _bytes, _p);
            print(std::cout); std::cout << '\n';
#endif
        } // do_deallocate

        auto do_is_equal(const boost::container::pmr::memory_resource& _other) const noexcept -> bool override
        {
            return this == &_other;
        } // do_is_equal

    private:
        // Header associated with an allocation within the underlying memory buffer.
        // All data segments will be preceded by a header.
        //
        // Memory Layout:
        //
        //     +------------------------------------------------------------------+
        //     | padding | header |                 data segment                  |
        //     +------------------+-----------------------------------------------+
        //                        | padding | unaligned pointer | aligned pointer |
        //                        +-----------------------------------------------+
        //
        struct header
        {
            std::size_t size;   // Size of the memory block (excluding all management info).
            header* prev;       // Pointer to the previous header block.
            header* next;       // Pointer to the next header block.
            bool used;          // Indicates whether the memory is in use.
        }; // struct header

        auto address_of_data_segment(header* _h) const noexcept -> ByteRep*
        {
            return reinterpret_cast<ByteRep*>(_h) + sizeof(header);
        } // address_of_data_segment

        auto aligned_alloc(std::size_t _bytes, std::size_t _alignment, header* _h)
            -> std::tuple<void*, std::size_t>
        {
            // The unused memory is located right after the header.
            void* data = address_of_data_segment(_h);
#ifdef IRODS_FIXED_BUFFER_RESOURCE_ENABLE_DEBUG
            fmt::print("unaligned data pointer = [{}]\n", fmt::ptr(data));
#endif
            auto space_left = _h->size - sizeof(void*);

            // Reserve space for the potentially unaligned pointer.
            void* aligned_data = static_cast<ByteRep*>(data) + sizeof(void*);

            if (!std::align(_alignment, _bytes, aligned_data, space_left)) {
                return {nullptr, 0};
            }

            // Store the address of the original allocation directly before the
            // aligned memory.
            new (static_cast<ByteRep*>(aligned_data) - sizeof(void*)) void*{data};

            return {aligned_data, space_left};
        } // aligned_alloc

        auto allocate_block(std::size_t _bytes, std::size_t _alignment, header* _h) -> void*
        {
            if (_h->used) {
                return nullptr;
            }

            // Return the block if it matches the requested number of bytes.
            if (_bytes == _h->size) {
                if (auto* aligned_data = std::get<void*>(aligned_alloc(_bytes, _alignment, _h)); aligned_data) {
                    _h->used = true;
                    allocated_ += _bytes;
                    return aligned_data;
                }
            }

            const auto max_space_needed = sizeof(header) + sizeof(void*) + _bytes + _alignment;

            // Split the data segment managed by this header if it is large enough
            // to satisfy the allocation request and management information.
            if (max_space_needed < _h->size) {
                auto [aligned_data, space_left] = aligned_alloc(_bytes, _alignment, _h);

                if (!aligned_data) {
                    return nullptr;
                }

                //std::size_t overhead = reinterpret_cast<ByteRep*>(aligned_data) - reinterpret_cast<ByteRep*>(_h);

                void* aligned_header_storage = static_cast<ByteRep*>(aligned_data) + _bytes;

                if (!std::align(alignof(header), sizeof(header), aligned_header_storage, space_left)) {
                    return nullptr;
                }

                const auto overhead_for_h = calculate_overhead(_h);

                // Construct a new header after the memory managed by "_h".
                // The new header manages unused memory.
                auto* new_header = new (aligned_header_storage) header;
                new_header->size = space_left - _bytes - sizeof(header);
                new_header->prev = _h;
                new_header->next = _h->next;
                new_header->used = false;

                // Update the allocation table links for the header just after the
                // newly added header.
                if (auto* next_header = _h->next; next_header) {
                    next_header->prev = new_header;
                }

                // Adjust the current header's size and mark it as used.
                _h->size = _bytes;
                _h->next = new_header;
                _h->used = true;

                allocated_ += _bytes;

                //overhead_ -= overhead_for_h;
                //overhead_ += calculate_overhead(_h);
                //overhead_ += calculate_overhead(new_header);

                return aligned_data;
            }

            return nullptr;
        } // allocate_block

        auto coalesce_with_next_unused_block(header* _h) -> void
        {
            if (!_h || _h->used) {
                return;
            }

            auto* header_to_remove = _h->next;
            const auto overhead_for_h = calculate_overhead(_h);
            const auto overhead_to_remove = calculate_overhead(_h->next);

            // Coalesce the memory blocks if they are not in use by the client.
            // This means that "_h" will absorb the header at "_h->next".
            if (header_to_remove && !header_to_remove->used) {
                _h->size += (header_to_remove->size + overhead_to_remove);
                _h->next = header_to_remove->next;

                // Make sure the links between the headers are updated appropriately.
                // (i.e. "_h" and "header_to_remove->next" need their links updated).
                if (auto* new_next_header = header_to_remove->next; new_next_header) {
                    new_next_header->prev = _h;
                }

                //overhead_ -= (overhead_for_h + overhead_to_remove);
                //overhead_ += calculate_overhead(_h);
            }
        } // coalesce_with_next_unused_block

        auto calculate_overhead(header* _h) const noexcept -> std::size_t
        {
            if (_h) {
                if (_h->next) {
                    const auto diff = reinterpret_cast<ByteRep*>(_h->next) - reinterpret_cast<ByteRep*>(_h) - _h->size;
                    //fmt::print("internal node diff = {}\n", diff);
                    return diff;
                }

                const auto end_of_buffer = reinterpret_cast<ByteRep*>(buffer_) + buffer_size_;
                //fmt::print("end_of_buffer = {}", end_of_buffer);
                //fmt::print("_h->size      = {}", _h->size);
                //fmt::print("_h - _h->size = {}", std::ptrdiff_t{reinterpret_cast<ByteRep*>(_h) - _h->size});

                const auto diff = end_of_buffer - reinterpret_cast<ByteRep*>(_h) - _h->size;
                //fmt::print("last node diff = {}\n", diff);
                return diff;
            }

            return 0;
        } // calculate_overhead

        void* buffer_;
        std::size_t buffer_size_;
        std::size_t allocated_;
        std::size_t overhead_;
        header* headers_;
    }; // fixed_buffer_resource
} // namespace irods::experimental::pmr

std::string random_string(std::size_t length)
{
    const auto randchar = []() -> char
    {
        const char charset[] =
        "0123456789"
        //"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        //"abcdefghijklmnopqrstuvwxyz"
        ;
        const size_t max_index = (sizeof(charset) - 1);
        return charset[rand() % max_index];
    };
    
    std::string str(length, 0);
    std::generate_n(str.begin(), length, randchar);
    
    return str;
}

template <typename Allocator>
auto do_test(Allocator& _allocator) -> void
{
    pmr::vector<pmr::string> strings{&_allocator};
    
    if constexpr (std::is_same_v<Allocator, irods::experimental::pmr::fixed_buffer_resource<std::byte>>) {
        //_allocator.print(std::cout); std::cout << '\n';
    }

    const auto start = std::chrono::system_clock::now();
    
    try {
        strings.reserve(1200000);
        for (int i = 0; i < 1200000; ++i) {
            //const auto s = random_string(6);
            //strings.emplace_back(s.c_str());
            //strings.emplace_back("0123456789012345678901234567890123456789012345678901234567890123456789");
            strings.emplace_back("012345");
        }
    }
    catch (const std::exception& e) {
        std::cout << e.what() << '\n';
    }

    const auto elapsed = std::chrono::system_clock::now() - start;
    const auto t = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    std::cout << "time elapsed             : " << t.count() << '\n';

    if constexpr (std::is_same_v<Allocator, irods::experimental::pmr::fixed_buffer_resource<std::byte>> ||
                  std::is_same_v<Allocator, capped_memory_pool>)
    {
        std::cout << "total memory allocated   : " << _allocator.allocated() << '\n';
        //std::cout << "total allocation overhead: " << _allocator.allocation_overhead() << '\n';
        //_allocator.print(std::cout); std::cout << '\n';
    }
}

int main()
{
    constexpr std::size_t max_size = 100000000;

    {
        capped_memory_pool cmp{max_size};
        pmr::unsynchronized_pool_resource uspr{&cmp};
        //do_test(cmp);
        do_test(uspr);
        std::cout << '\n';
    }

    std::vector<std::byte> buffer(max_size);
    irods::experimental::pmr::fixed_buffer_resource fbr(buffer.data(), buffer.size());
    pmr::unsynchronized_pool_resource uspr{&fbr};

    //do_test(fbr);
    do_test(uspr);

    std::cout << "\nPost Test:\n";
    std::cout << "  total memory allocated   : " << fbr.allocated() << '\n';
    std::cout << "  total allocation overhead: " << fbr.allocation_overhead() << '\n';

    return 0;
}

