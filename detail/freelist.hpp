//  lock-free freelist
//
//  Copyright (C) 2008-2016 Tim Blechmann
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_LOCKFREE_FREELIST_HPP_INCLUDED
#define BOOST_LOCKFREE_FREELIST_HPP_INCLUDED

#include <limits>
#include <memory>

#include <boost/array.hpp>
#include <boost/config.hpp>
#include <boost/cstdint.hpp>
#include <boost/noncopyable.hpp>
#include <boost/static_assert.hpp>

#include <boost/align/align_up.hpp>
#include <boost/align/aligned_allocator_adaptor.hpp>

#include <boost/lockfree/detail/atomic.hpp>
#include <boost/lockfree/detail/parameter.hpp>
#include <boost/lockfree/detail/tagged_ptr.hpp>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4127) // conditional expression is constant
#endif

namespace boost    {
namespace lockfree {
namespace detail   {

template <typename T,
          typename Alloc = std::allocator<T>
         >
class freelist_stack:
    Alloc
{
    struct freelist_node
    {
        tagged_ptr<freelist_node> next;
    };

    typedef tagged_ptr<freelist_node> tagged_node_ptr;

public:
    typedef T *           index_t;
    typedef tagged_ptr<T> tagged_node_handle;

    template <typename Allocator>
    freelist_stack (Allocator const & alloc, std::size_t n = 0):
        Alloc(alloc),
        pool_(tagged_node_ptr(NULL))
    {
        for (std::size_t i = 0; i != n; ++i) {
            T * node = Alloc::allocate(1);
#ifdef BOOST_LOCKFREE_FREELIST_INIT_RUNS_DTOR
            destruct<false>(node);
#else
            deallocate<false>(node);
#endif
        }
    }

    template <bool ThreadSafe>
    void reserve (std::size_t count)
    {
        for (std::size_t i = 0; i != count; ++i) {
            T * node = Alloc::allocate(1);
            deallocate<ThreadSafe>(node);
        }
    }

    template <bool ThreadSafe, bool Bounded>
    T * construct (void)
    {
        T * node = allocate<ThreadSafe, Bounded>();
        if (node)
            new(node) T();
        return node;
    }

    template <bool ThreadSafe, bool Bounded, typename ArgumentType>
    T * construct (ArgumentType const & arg)
    {
        T * node = allocate<ThreadSafe, Bounded>();
        if (node)
            new(node) T(arg);
        return node;
    }

    template <bool ThreadSafe, bool Bounded, typename ArgumentType1, typename ArgumentType2>
    T * construct (ArgumentType1 const & arg1, ArgumentType2 const & arg2)
    {
        T * node = allocate<ThreadSafe, Bounded>();
        if (node)
            new(node) T(arg1, arg2);
        return node;
    }

    template <bool ThreadSafe>
    void destruct (tagged_node_handle const & tagged_ptr)
    {
        T * n = tagged_ptr.get_ptr();
        n->~T();
        deallocate<ThreadSafe>(n);
    }

    template <bool ThreadSafe>
    void destruct (T * n)
    {
        n->~T();
        deallocate<ThreadSafe>(n);
    }

    ~freelist_stack(void)
    {
        tagged_node_ptr current = pool_.load();

        while (current) {
            freelist_node * current_ptr = current.get_ptr();
            if (current_ptr)
                current = current_ptr->next;
            Alloc::deallocate((T*)current_ptr, 1);
        }
    }

    bool is_lock_free(void) const
    {
        return pool_.is_lock_free();
    }

    T * get_handle(T * pointer) const
    {
        return pointer;
    }

    T * get_handle(tagged_node_handle const & handle) const
    {
        return get_pointer(handle);
    }

    T * get_pointer(tagged_node_handle const & tptr) const
    {
        return tptr.get_ptr();
    }

    T * get_pointer(T * pointer) const
    {
        return pointer;
    }

    T * null_handle(void) const
    {
        return NULL;
    }

protected: // allow use from subclasses
    template <bool ThreadSafe, bool Bounded>
    T * allocate (void)
    {
        if (ThreadSafe)
            return allocate_impl<Bounded>();
        else
            return allocate_impl_unsafe<Bounded>();
    }

private:
    template <bool Bounded>
    T * allocate_impl (void)
    {
        tagged_node_ptr old_pool = pool_.load(memory_order_consume);

        for(;;) {
            if (!old_pool.get_ptr()) {
                if (!Bounded)
                    return Alloc::allocate(1);
                else
                    return 0;
            }

            freelist_node * new_pool_ptr = old_pool->next.get_ptr();
            tagged_node_ptr new_pool (new_pool_ptr, old_pool.get_next_tag());

            if (pool_.compare_exchange_weak(old_pool, new_pool)) {
                void * ptr = old_pool.get_ptr();
                return reinterpret_cast<T*>(ptr);
            }
        }
    }

    template <bool Bounded>
    T * allocate_impl_unsafe (void)
    {
        tagged_node_ptr old_pool = pool_.load(memory_order_relaxed);

        if (!old_pool.get_ptr()) {
            if (!Bounded)
                return Alloc::allocate(1);
            else
                return 0;
        }

        freelist_node * new_pool_ptr = old_pool->next.get_ptr();
        tagged_node_ptr new_pool (new_pool_ptr, old_pool.get_next_tag());

        pool_.store(new_pool, memory_order_relaxed);
        void * ptr = old_pool.get_ptr();
        return reinterpret_cast<T*>(ptr);
    }

protected:
    template <bool ThreadSafe>
    void deallocate (T * n)
    {
        if (ThreadSafe)
            deallocate_impl(n);
        else
            deallocate_impl_unsafe(n);
    }

private:
    void deallocate_impl (T * n)
    {
        void * node = n;
        tagged_node_ptr old_pool = pool_.load(memory_order_consume);
        freelist_node * new_pool_ptr = reinterpret_cast<freelist_node*>(node);

        for(;;) {
            tagged_node_ptr new_pool (new_pool_ptr, old_pool.get_tag());
            new_pool->next.set_ptr(old_pool.get_ptr());

            if (pool_.compare_exchange_weak(old_pool, new_pool))
                return;
        }
    }

    void deallocate_impl_unsafe (T * n)
    {
        void * node = n;
        tagged_node_ptr old_pool = pool_.load(memory_order_relaxed);
        freelist_node * new_pool_ptr = reinterpret_cast<freelist_node*>(node);

        tagged_node_ptr new_pool (new_pool_ptr, old_pool.get_tag());
        new_pool->next.set_ptr(old_pool.get_ptr());

        pool_.store(new_pool, memory_order_relaxed);
    }

    atomic<tagged_node_ptr> pool_;
};

class
BOOST_ALIGNMENT( 4 ) // workaround for bugs in MSVC
tagged_index
{
public:
    typedef boost::uint16_t tag_t;
    typedef boost::uint16_t index_t;

    /** uninitialized constructor */
    tagged_index(void) BOOST_NOEXCEPT //: index(0), tag(0)
    {}

    /** copy constructor */
#ifdef BOOST_NO_CXX11_DEFAULTED_FUNCTIONS
    tagged_index(tagged_index const & rhs):
        index(rhs.index), tag(rhs.tag)
    {}
#else
    tagged_index(tagged_index const & rhs) = default;
#endif

    explicit tagged_index(index_t i, tag_t t = 0):
        index(i), tag(t)
    {}

    /** index access */
    /* @{ */
    index_t get_index() const
    {
        return index;
    }

    void set_index(index_t i)
    {
        index = i;
    }
    /* @} */

    /** tag access */
    /* @{ */
    tag_t get_tag() const
    {
        return tag;
    }

    tag_t get_next_tag() const
    {
        tag_t next = (get_tag() + 1u) & (std::numeric_limits<tag_t>::max)();
        return next;
    }

    void set_tag(tag_t t)
    {
        tag = t;
    }
    /* @} */

    bool operator==(tagged_index const & rhs) const
    {
        return (index == rhs.index) && (tag == rhs.tag);
    }

    bool operator!=(tagged_index const & rhs) const
    {
        return !operator==(rhs);
    }

protected:
    index_t index;
    tag_t tag;
};

template <typename T,
          std::size_t size>
struct compiletime_sized_freelist_storage
{
    // array-based freelists only support a 16bit address space.
    BOOST_STATIC_ASSERT(size < 65536);

    boost::array<char, size * sizeof(T) + 64> data;

    // unused ... only for API purposes
    template <typename Allocator>
    compiletime_sized_freelist_storage(Allocator const & /* alloc */, std::size_t /* count */)
    {}

    T * nodes(void) const
    {
        char * data_pointer = const_cast<char*>(data.data());
        return reinterpret_cast<T*>( boost::alignment::align_up( data_pointer, BOOST_LOCKFREE_CACHELINE_BYTES ) );
    }

    std::size_t node_count(void) const
    {
        return size;
    }
};

template <typename T,
          typename Alloc = std::allocator<T> >
struct runtime_sized_freelist_storage:
    boost::alignment::aligned_allocator_adaptor<Alloc, BOOST_LOCKFREE_CACHELINE_BYTES >
{
    // 用aligned allocator adaptor来分配node内存
    typedef boost::alignment::aligned_allocator_adaptor<Alloc, BOOST_LOCKFREE_CACHELINE_BYTES > allocator_type;
    T * nodes_;
    std::size_t node_count_;

    template <typename Allocator>
    runtime_sized_freelist_storage(Allocator const & alloc, std::size_t count):
        allocator_type(alloc), node_count_(count)
    {
        if (count > 65535)
            boost::throw_exception(std::runtime_error("boost.lockfree: freelist size is limited to a maximum of 65535 objects"));
        nodes_ = allocator_type::allocate(count);
    }

    ~runtime_sized_freelist_storage(void)
    {
        allocator_type::deallocate(nodes_, node_count_);
    }

    T * nodes(void) const
    {
        return nodes_;
    }

    std::size_t node_count(void) const
    {
        return node_count_;
    }
};


template <typename T,
          typename NodeStorage = runtime_sized_freelist_storage<T>
         >
class fixed_size_freelist:
    NodeStorage
{
    struct freelist_node
    {
        tagged_index next;
    };

    void initialize(void)
    {
        T * nodes = NodeStorage::nodes();
        for (std::size_t i = 0; i != NodeStorage::node_count(); ++i) {
            tagged_index * next_index = reinterpret_cast<tagged_index*>(nodes + i);
            next_index->set_index(null_handle());

#ifdef BOOST_LOCKFREE_FREELIST_INIT_RUNS_DTOR
            destruct<false>(nodes + i);
#else
            deallocate<false>(static_cast<index_t>(i));
#endif
        }
    }

public:
    typedef tagged_index tagged_node_handle;
    typedef tagged_index::index_t index_t;

    template <typename Allocator>
    fixed_size_freelist (Allocator const & alloc, std::size_t count):
        NodeStorage(alloc, count),
        pool_(tagged_index(static_cast<index_t>(count), 0))
    {
        initialize();
    }

    fixed_size_freelist (void):
        pool_(tagged_index(NodeStorage::node_count(), 0))
    {
        initialize();
    }

    template <bool ThreadSafe, bool Bounded>
    T * construct (void)
    {
        // 从后台存储空间中拿到一个可用节点索引
        index_t node_index = allocate<ThreadSafe>();
        if (node_index == null_handle())
            return NULL;

        // 直接在之前分配的存储空间上构造对象T
        // placement new构造
        T * node = NodeStorage::nodes() + node_index;
        new(node) T();
        return node;
    }

    // 构造函数带一个参数的版本
    template <bool ThreadSafe, bool Bounded, typename ArgumentType>
    T * construct (ArgumentType const & arg)
    {
        index_t node_index = allocate<ThreadSafe>();
        if (node_index == null_handle())
            return NULL;

        T * node = NodeStorage::nodes() + node_index;
        new(node) T(arg);
        return node;
    }

    // 构造函数带两个参数的版本
    template <bool ThreadSafe, bool Bounded, typename ArgumentType1, typename ArgumentType2>
    T * construct (ArgumentType1 const & arg1, ArgumentType2 const & arg2)
    {
        index_t node_index = allocate<ThreadSafe>();
        if (node_index == null_handle())
            return NULL;

        T * node = NodeStorage::nodes() + node_index;
        new(node) T(arg1, arg2);
        return node;
    }

    template <bool ThreadSafe>
    void destruct (tagged_node_handle tagged_index)
    {
        index_t index = tagged_index.get_index();
        T * n = NodeStorage::nodes() + index;
        (void)n; // silence msvc warning
        n->~T();
        deallocate<ThreadSafe>(index);
    }

    template <bool ThreadSafe>
    void destruct (T * n)
    {
        n->~T();
        deallocate<ThreadSafe>(static_cast<index_t>(n - NodeStorage::nodes()));
    }

    bool is_lock_free(void) const
    {
        return pool_.is_lock_free();
    }

    index_t null_handle(void) const
    {
        return static_cast<index_t>(NodeStorage::node_count());
    }

    index_t get_handle(T * pointer) const
    {
        if (pointer == NULL)
            return null_handle();
        else
            return static_cast<index_t>(pointer - NodeStorage::nodes());
    }

    index_t get_handle(tagged_node_handle const & handle) const
    {
        return handle.get_index();
    }

    T * get_pointer(tagged_node_handle const & tptr) const
    {
        return get_pointer(tptr.get_index());
    }

    T * get_pointer(index_t index) const
    {
        if (index == null_handle())
            return 0;
        else
            return NodeStorage::nodes() + index;
    }

    T * get_pointer(T * ptr) const
    {
        return ptr;
    }

protected: // allow use from subclasses
    template <bool ThreadSafe>
    index_t allocate (void)
    {
        if (ThreadSafe)
            return allocate_impl();
        else
            return allocate_impl_unsafe();
    }

private:
    index_t allocate_impl (void)
    {
        tagged_index old_pool = pool_.load(memory_order_consume);

        for(;;) {
            // 链表为空？
            // 如果此时另外一个线程已经刚好更新完pool_，
            // 那么此时old_pool指向的节点已经被那个线程拿去用了，数据可能已经非常不一样了
            // 后面所有的get_index将可能获取到非常错误的数据
            // 但是没关系，下面的代码`index`始终是对的，只是next_pool可能数据非常不对
            // compare_exchange_weak不会成功
            // 但是有没可能在这个线程运行到570之前，其它线程把pool_又变回来了，跟原来一样？
            // 拿出去的节点会换回来了？
            // 不会！因为每次新的pool头节点的tag都会递增`old_pool.get_next_tag()`
            // 都是从老pool头节点tag递推出来，下次新的pool_与原来的不一样了，哪怕index是一样
            index_t index = old_pool.get_index();
            if (index == null_handle())
                return index;

            // 不为空，链表头作为待分配的节点
            T * old_node = NodeStorage::nodes() + index;
            // 下一个链表节点作为接下来新的链表头
            tagged_index * next_index = reinterpret_cast<tagged_index*>(old_node);
            // 构造新链表头，每个tagged_index存储下一个节点的索引
            tagged_index new_pool(next_index->get_index(), old_pool.get_next_tag());
            // 如果当前链表头没发生变化=old_pool，则原子的设置新链表头，返回老链表头节点索引
            // 否则old_pool更新，重做上面的步骤
            if (pool_.compare_exchange_weak(old_pool, new_pool))
                return old_pool.get_index();
        }
    }

    index_t allocate_impl_unsafe (void)
    {
        tagged_index old_pool = pool_.load(memory_order_consume);

        index_t index = old_pool.get_index();
        if (index == null_handle())
            return index;

        T * old_node = NodeStorage::nodes() + index;
        tagged_index * next_index = reinterpret_cast<tagged_index*>(old_node);

        tagged_index new_pool(next_index->get_index(), old_pool.get_next_tag());

        // 新的链表头将会是老链表头的下一个节点
        pool_.store(new_pool, memory_order_relaxed);
        return old_pool.get_index();
    }

    template <bool ThreadSafe>
    void deallocate (index_t index)
    {
        if (ThreadSafe)
            deallocate_impl(index);
        else
            deallocate_impl_unsafe(index);
    }

    void deallocate_impl (index_t index)
    {
        freelist_node * new_pool_node = reinterpret_cast<freelist_node*>(NodeStorage::nodes() + index);
        tagged_index old_pool = pool_.load(memory_order_consume);

        // 从链表头插入一个回收节点
        // 先获取链表头`old_pool`，之后准备新链表头
        // 原子性的转移新链表头
        for(;;) {
            // 回收时，新pool头节点的tag与老pool头节点tag一样
            // 已经足够，因为index肯定不一样，最终节点会不一样
            // 但是分配时，新pool头节点的tag与老pool头节点tag不一样
            // 所以同一个头节点分配出去，回收回来，index一样，但是tag会不一样
            tagged_index new_pool (index, old_pool.get_tag());
            new_pool_node->next.set_index(old_pool.get_index());

            if (pool_.compare_exchange_weak(old_pool, new_pool))
                return;
        }
    }

    void deallocate_impl_unsafe (index_t index)
    {
        freelist_node * new_pool_node = reinterpret_cast<freelist_node*>(NodeStorage::nodes() + index);
        tagged_index old_pool = pool_.load(memory_order_consume);

        tagged_index new_pool (index, old_pool.get_tag());
        new_pool_node->next.set_index(old_pool.get_index());

        pool_.store(new_pool);
    }

    // 链表头，tagged_index是一个结构体，align到4个字节，支持原子性
    atomic<tagged_index> pool_;
};

template <typename T,
          typename Alloc,
          bool IsCompileTimeSized,
          bool IsFixedSize,
          std::size_t Capacity
          >
struct select_freelist
{
    // 编译时期的freelist storage，一般就会是采用array来存储node
    // 运行时期的固定freelist storage，一般就是一开始new一段内存空间存储node
    typedef typename mpl::if_c<IsCompileTimeSized,
                               compiletime_sized_freelist_storage<T, Capacity>,
                               runtime_sized_freelist_storage<T, Alloc>
                              >::type fixed_sized_storage_type;
    // 如果是编译期确定大小或者固定大小的，会采用fixed_size_freelist
    // 否则就走free_stack
    typedef typename mpl::if_c<IsCompileTimeSized || IsFixedSize,
                               fixed_size_freelist<T, fixed_sized_storage_type>,
                               freelist_stack<T, Alloc>
                              >::type type;
};

template <typename T, bool IsNodeBased>
struct select_tagged_handle
{
    typedef typename mpl::if_c<IsNodeBased,
                               tagged_ptr<T>,
                               tagged_index
                              >::type tagged_handle_type;

    typedef typename mpl::if_c<IsNodeBased,
                               T*,
                               typename tagged_index::index_t
                              >::type handle_type;
};


} /* namespace detail */
} /* namespace lockfree */
} /* namespace boost */

#if defined(_MSC_VER)
#pragma warning(pop)
#endif


#endif /* BOOST_LOCKFREE_FREELIST_HPP_INCLUDED */
