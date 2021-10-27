# if 1

#include <cstddef> // for std::byte
#include <cassert>
#include <iostream>
#include <vector>
#include <stdexcept>

#include <boost/container/pmr/memory_resource.hpp>
#include <boost/container/pmr/vector.hpp>
#include <boost/container/pmr/string.hpp>

namespace pmr = boost::container::pmr;

template <typename T> // T is the byte representation.
class fixed_buffer_resource
    : public pmr::memory_resource
{
public:
    fixed_buffer_resource(T* _buffer, std::size_t _size)
        : pmr::memory_resource{}
        , buffer_{_buffer}
        , buffer_size_{_size}
        , allocated_{}
        , blocks_{}
    {
        assert(_buffer != nullptr);
        assert(_size > 0);
        
        blocks_ = reinterpret_cast<block*>(buffer_);
        blocks_->hdr.size = buffer_size_ - sizeof(block);// + sizeof(T);
        blocks_->hdr.prev = nullptr;
        blocks_->hdr.next = nullptr;
        blocks_->hdr.is_used = false;
        blocks_->data = buffer_ + sizeof(header);
        
        std::cout << "sizeof(block) = " << sizeof(block) << '\n';
        std::cout << "sizeof(T)             = " << sizeof(T) << '\n';
        std::cout << "freelist ["
                  << "size = " << blocks_->hdr.size
                  << ", prev = " << blocks_->hdr.prev
                  << ", next = " << blocks_->hdr.next
                  << ", is_used = " << blocks_->hdr.is_used
                  << ", data = " << static_cast<void*>(blocks_->data)
                  << "]\n";
    }
    
    fixed_buffer_resource(const fixed_buffer_resource&) = delete;
    fixed_buffer_resource& operator=(const fixed_buffer_resource&) = delete;
    
    std::size_t allocated() const noexcept
    {
        return allocated_;
    }

protected:
    struct block;
    
    struct header
    {
        std::size_t size;       
        block* prev;
        block* next;
        bool is_used;
    };
    
    struct block
    {
        header hdr;
        T* data;
    };

    void* do_allocate(std::size_t _bytes, std::size_t _alignment) override
    {
        std::cout << "allocating " << _bytes << " byte(s) with an alignment of " << _alignment << '\n';

        // Check the freelist.
        for (auto* node = blocks_; node; node = node->hdr.next) {
            if (node->hdr.is_used) {
                continue;
            }

            // Found a node that matches the size exactly.
            // Just return it.
            if (_bytes == node->hdr.size) {
                std::cout << "Returning freelist node matching exact allocation size ...\n";
                node->hdr.is_used = true;
                allocated_ += _bytes;
                return node->data;
            }

            // Split the space and insert a new block.
            // In order to split the node, the node's size must be large
            // enough to hold a block.
            if (sizeof(block) + _bytes <= node->hdr.size) {
                std::cout << "Splitting freelist node ...\n";

                // Create a new node and insert it after the current node.
                // This new node holds unused memory.
                auto* new_node = reinterpret_cast<block*>(node->data + _bytes);
                new_node->hdr.size = node->hdr.size - _bytes;
                new_node->hdr.prev = node;
                new_node->hdr.next = node->hdr.next;
                new_node->hdr.is_used = false;
                new_node->data = reinterpret_cast<T*>(new_node) + sizeof(header);

                // Adjust the current node's size and mark it as used.
                node->hdr.size = _bytes;
                node->hdr.next = new_node;
                node->hdr.is_used = true;

                allocated_ += _bytes;

                //std::cout << "node address          = " << (void*) node << '\n';
                //std::cout << "node + sizeof(header) = " << (void*) (node + sizeof(header)) << '\n';
                //std::cout << "node data address     = " << (void*) node->data << '\n';

                return node->data;
            }
            else {
                std::cout << "not error space for block and allocation.\n";
            }
        }

        throw std::bad_alloc{};
    }
    
    void do_deallocate(void* _p, std::size_t _bytes, std::size_t _alignment) override
    {
        std::cout << "deallocating [" << _p << "]: " << _bytes << " byte(s) with an alignment of " << _alignment << '\n';

        // Throw an exception if the pointer didn't come from this
        // memory resource.
        if (_p < buffer_ || _p > buffer_ + buffer_size_) {
            throw std::runtime_error{"pointer did not come from this memory resource."};
        }

        //  Add memory to the freelist.
        auto* node = reinterpret_cast<block*>(static_cast<T*>(_p) - sizeof(header));
        //std::cout << "node address      = " << (void*) node << '\n';
        //std::cout << "sizeof(header)    = " << sizeof(header) << '\n';
        //std::cout << "node->hdr.is_used = " << node->hdr.is_used << '\n';
        //std::cout << "node->hdr.size    = " << node->hdr.size << '\n';
        assert(node->hdr.is_used == true);
        assert(node->hdr.size == _bytes);

        node->hdr.is_used = false;

        // Coalese adjacent blocks.
        coalesce_with_next_node(node);
        coalesce_with_next_node(node->hdr.prev);

        allocated_ -= _bytes;
    }

    bool do_is_equal(const pmr::memory_resource& _other) const noexcept override
    {
        return this == &_other;
    }

private:
    void coalesce_with_next_node(block* _node)
    {
        if (!_node) {
            return;
        }
        
        auto* next_node = _node->hdr.next;
        
        if (next_node && !next_node->hdr.is_used) {
            _node->hdr.size += next_node->hdr.size;
            _node->hdr.next = next_node->hdr.next;
        }
    }

    T* buffer_;
    std::size_t buffer_size_;
    std::size_t allocated_;
    block* blocks_;
}; // fixed_buffer_resource

int main()
{
    std::vector<char> buffer(200);
    fixed_buffer_resource fbr{buffer.data(), buffer.size()};
    
    try {
        pmr::vector<pmr::string> v{&fbr};
        v.reserve(3);
        for (int i = 0; i < 3; ++i) {
            v.emplace_back("kory");
            std::cout << "total elements: " << v.size() << '\n';
            std::cout << "total memory allocated: " << fbr.allocated() << '\n';
            std::cout << '\n';
        }
    }
    catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << '\n';
    }

    std::cout << "total memory allocated: " << fbr.allocated() << '\n';
    
    return 0;
}

#else

#include <iostream>
#include <vector>
#include <list>
#include <iterator>
#include <algorithm>

#include <boost/container/pmr/vector.hpp>
#include <boost/container/pmr/string.hpp>
#include <boost/container/pmr/unsynchronized_pool_resource.hpp>
#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/container/pmr/global_resource.hpp>

namespace pmr = boost::container::pmr;

#if 0
class fixed_buffer_resource final
    : public pmr::memory_resource
{
public:
    fixed_buffer_resource(char* _buffer, std::size_t _buffer_size)
        : pmr::memory_resource{}
        , buf_{_buffer}
        , buf_size_{_buffer_size}
        , alloc_ptr_{_buffer}
        , freelist_{}
        , mem_used_{}
    {
        std::fill(buf_, buf_ + buf_size_, 0);
    }

    fixed_buffer_resource(const fixed_buffer_resource&) = delete;
    auto operator=(const fixed_buffer_resource&) -> fixed_buffer_resource& = delete;

    ~fixed_buffer_resource() = default;

protected:
    struct free_block
    {
        int start_index = 0;
        int size = 0;
    };

    auto do_allocate(std::size_t _bytes, std::size_t _alignment) -> void* override
    {
        // Throw if the pending allocation cannot be satisfied.
        if (mem_used_ + _bytes >= buf_size_) {
            std::cerr << "not enough memory\n";
            throw std::bad_alloc{};
        }

        // Return an entry from the freelist.
        if (!freelist_.empty()) {
            const auto iter = std::find_if(begin(freelist_), end(freelist_), [_bytes](const auto& _node) {
                return _bytes <= _node.size;
            });

            if (iter != end(freelist_)) {
                // This node satisfies the memory allocation requirements.
                // Capture the address of the byte in the buffer and remove this
                // entry from the freelist.
                auto* t = &buf_[iter->start_index];
                freelist_.erase(iter);
                return t;
            }
        }

        // TODO How do we determine where the next free block is?
        // This implementation is pretty bad because the allocation pointer is
        // always moved forward. Instead, it should be able to scan the buffer
        // for unused memory and return that.
        auto* tmp = alloc_ptr_; // The address of the memory to return.
        alloc_ptr_ += _bytes;   // Bump the allocation pointer.
        mem_used_ += _bytes;    // Bump the amount of memory used.

        return tmp;
    }

    auto do_deallocate(void* _p, std::size_t _bytes, std::size_t _alignment) -> void override
    {
        // _p must be inbounds of the memory chunk.
        if (_p < buf_ || _p >= buf_ + buf_size_) {
            std::cerr << "pointer to deallocate did not come from this memory resource!\n";
            throw std::runtime_error{"deallocation error"};
        }

        if (auto* tp = static_cast<char*>(_p); tp < alloc_ptr_) {
            freelist_.emplace_back(tp - buf_, _bytes);
            return;
        }
    }

    auto do_is_equal(const pmr::memory_resource& _other) const noexcept -> bool override
    {
        return this == &_other;
    }

private:
    char* buf_;
    std::size_t buf_size_;

    char* alloc_ptr_;

    // Holds information about free blocks in the buffer.
    std::list<free_block> freelist_;

    std::size_t mem_used_;
}; // fixed_buffer_resource
#endif

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

    std::size_t total_bytes_allocated() const noexcept
    {
        return allocated_;
    }

protected:
    void* do_allocate(std::size_t _bytes, std::size_t) override
    {
        if (allocated_ + _bytes > max_size_)
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

    bool do_is_equal(const pmr::memory_resource& _other) const noexcept override
    {
        auto* p = dynamic_cast<const capped_memory_pool*>(&_other);
        return p && p->max_size_ == max_size_ && p->allocated_ == allocated_;
    }

private:
    std::size_t max_size_;
    std::size_t allocated_;
}; // capped_memory_resource

int main()
{
    //char buffer[5]{};
    //fixed_buffer_resource mr{buffer, sizeof(buffer)};
    //pmr::monotonic_buffer_resource mbr{buffer, sizeof(buffer) - 1, pmr::null_memory_resource()};

    //pmr::pool_options opts;
    //opts.max_blocks_per_chunk = sizeof(buffer);
    //opts.largest_required_pool_block = sizeof(buffer);
    //pmr::unsynchronized_pool_resource uspr{opts, &mbr};
    ////pmr::unsynchronized_pool_resource uspr{&mbr};

    capped_memory_pool mr{100};

    {
        pmr::vector<pmr::string> strings{&mr};
        strings.reserve(3);

        try {
            for (int i = 0; i < 4; ++i) {
                strings.emplace_back("abc");
                std::cout << "strings has " << strings.size() << " element(s) in it.\n";
                std::cout << "total bytes allocated: " << mr.total_bytes_allocated() << '\n';
            }
        }
        catch (const std::bad_alloc& e) {
            std::cout << e.what() << '\n';
        }

        for (auto&& s : strings)
            std::cout << "string: " << s << '\n';

        //std::cout << buffer << '\n';
        std::cout << "total bytes allocated: " << mr.total_bytes_allocated() << '\n';
    }

    std::cout << "total bytes allocated: " << mr.total_bytes_allocated() << '\n';

    //strings.clear();
    //for (int i = 0; i < 5; ++i) {
    //    strings.push_back('Y');
    //    std::cout << "strings has " << strings.size() << " elements in it.\n";
    //}
}

#endif
