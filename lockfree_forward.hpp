//  Copyright (C) 2008-2016 Tim Blechmann
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)


#ifndef BOOST_LOCKFREE_FORWARD_HPP_INCLUDED
#define BOOST_LOCKFREE_FORWARD_HPP_INCLUDED


#ifndef BOOST_DOXYGEN_INVOKED

#include <cstddef> // size_t

#include <boost/config.hpp>

#ifdef BOOST_NO_CXX11_VARIADIC_TEMPLATES
#include <boost/parameter/aux_/void.hpp>
#endif

namespace boost    {
namespace lockfree {

// policies
template <bool IsFixedSized>
struct fixed_sized;

template <size_t Size>
struct capacity;

template <class Alloc>
struct allocator;


// data structures

#ifdef BOOST_NO_CXX11_VARIADIC_TEMPLATES
template <typename T,
          class A0 = boost::parameter::void_,
          class A1 = boost::parameter::void_,
          class A2 = boost::parameter::void_>
#else
template <typename T, typename ...Options>
#endif
class queue;  // multiple producers, multiple consumers的队列

#ifdef BOOST_NO_CXX11_VARIADIC_TEMPLATES
template <typename T,
          class A0 = boost::parameter::void_,
          class A1 = boost::parameter::void_,
          class A2 = boost::parameter::void_>
#else
template <typename T, typename ...Options>
#endif
class stack;  // lock-free栈：支持固定大小的stack，或者自动扩容的stack

#ifdef BOOST_NO_CXX11_VARIADIC_TEMPLATES
template <typename T,
          class A0 = boost::parameter::void_,
          class A1 = boost::parameter::void_>
#else
template <typename T, typename ...Options>
#endif
class spsc_queue;  // single producer，single consumer的队列

}
}

#endif // BOOST_DOXYGEN_INVOKED

#endif // BOOST_LOCKFREE_FORWARD_HPP_INCLUDED
