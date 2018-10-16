#ifndef XENIUM_HARRIS_MICHAEL_HASH_MAP_HPP
#define XENIUM_HARRIS_MICHAEL_HASH_MAP_HPP

#include <xenium/acquire_guard.hpp>
#include <xenium/backoff.hpp>

#include <atomic>
#include <functional>

namespace xenium {

/**
 * @brief A generic lock-free hash-map.
 *
 * This hash-map consists of a fixed number of buckets were each bucket is essentially
 * a `harris_michael_list_based_set` instance. The number of buckets is fixed, so the
 * hash-map does not support dynamic resizing.
 * 
 * This hash-map is less efficient than many other available concurrent hash-maps, but it is
 * lock-free and fully generic, i.e., it supports arbitrary types for `Key` and `Value`.
 * 
 * This data structure is based on the solution proposed by [Michael]
 * (http://www.liblfds.org/downloads/white%20papers/%5BHash%5D%20-%20%5BMichael%5D%20-%20High%20Performance%20Dynamic%20Lock-Free%20Hash%20Tables%20and%20List-Based%20Sets.pdf)
 * which builds upon the original proposal by [Harris]
 * (https://www.cl.cam.ac.uk/research/srg/netos/papers/2001-caslists.pdf). 
 *
 * @tparam Key
 * @tparam Value
 * @tparam Reclaimer the reclamation scheme to use for internally created nodes.
 * @tparam Buckets the number of buckets.
 * @tparam Backoff the backoff stragtey to be used; defaults to `no_backoff`.
 */
template <class Key, class Value, class Reclaimer, size_t Buckets, class Backoff = no_backoff>
class harris_michael_hash_map
{
public:
  using value_type = std::pair<const Key, Value>;

  class iterator;

  harris_michael_hash_map() = default;
  ~harris_michael_hash_map();

  /**
   * @brief Inserts a new element into the container if the container doesn't already contain an
   * element with an equivalent key. The element is constructed in-place with the given `args`.
   *
   * The element is always constructed. If there already is an element with the key in the container,
   * the newly constructed element will be destroyed immediately.
   *
   * No iterators or references are invalidated.
   * 
   * Progress guarantees: lock-free
   * 
   * @param args arguments to forward to the constructor of the element
   * @return `true` if an element was inserted, otherwise `false`
   */
  template <class... Args>
  bool emplace(Args&&... args);

  /**
   * @brief Inserts a new element into the container if the container doesn't already contain an
   * element with an equivalent key. The element is constructed in-place with the given `args`.
   *
   * The element is always constructed. If there already is an element with the key in the container,
   * the newly constructed element will be destroyed immediately.
   *
   * No iterators or references are invalidated.
   * 
   * Progress guarantees: lock-free
   * 
   * @param args arguments to forward to the constructor of the element
   * @return a pair consisting of an iterator to the inserted element, or the already-existing element
   * if no insertion happened, and a bool denoting whether the insertion took place;
   * `true` if an element was inserted, otherwise `false`
   */
  template <class... Args>
  std::pair<iterator, bool> emplace_or_get(Args&&... args);

  /**
   * @brief Inserts a new element into the container if the container doesn't already contain an
   * element with an equivalent key. The element is constructed as `value_type(std::piecewise_construct,
   * std::forward_as_tuple(k), std::forward_as_tuple(std::forward<Args>(args)...))`.
   *
   * The element may be constructed even if there already is an element with the key in the container,
   * in which case the newly constructed element will be destroyed immediately.
   *
   * No iterators or references are invalidated.
   * Progress guarantees: lock-free
   *
   * @param key the key of element to be inserted.
   * @param args arguments to forward to the constructor of the element
   * @return a pair consisting of an iterator to the inserted element, or the already-existing element
   * if no insertion happened, and a bool denoting whether the insertion took place;
   * `true` if an element was inserted, otherwise `false`
   */
  template <class... Args>
  std::pair<iterator, bool> get_or_emplace(Key key, Args&&... args);

  /**
   * @brief Inserts a new element into the container if the container doesn't already contain an
   * element with an equivalent key. The value for the newly constructed element is created by
   * calling `value_factory`.
   *
   * The element may be constructed even if there already is an element with the key in the container,
   * in which case the newly constructed element will be destroyed immediately.
   *
   * No iterators or references are invalidated.
   * Progress guarantees: lock-free
   *
   * @tparam Func
   * @param key the key of element to be inserted.
   * @param value_factory a functor that is used to create the `Value` instance when constructing
   * the new element to be inserted.
   * @return a pair consisting of an iterator to the inserted element, or the already-existing element
   * if no insertion happened, and a bool denoting whether the insertion took place;
   * `true` if an element was inserted, otherwise `false`
   */
  template <class Factory>
  std::pair<iterator, bool> get_or_emplace_lazy(Key key, Factory factory);

  /**
   * @brief Removes the element with the key equivalent to key (if one exists).
   *
   * No iterators or references are invalidated.
   * 
   * Progress guarantees: lock-free
   * 
   * @param key key of the element to remove
   * @return `true` if an element was removed, otherwise `false`
   */
  bool erase(const Key& key);

  /**
   * @brief Removes the specified element from the container.
   *
   * No iterators or references are invalidated.
   * 
   * Progress guarantees: lock-free
   * 
   * @param pos the iterator identifying the element to remove
   * @return iterator following the last removed element
   */
  iterator erase(iterator pos);

  /**
   * @brief Finds an element with key equivalent to key.
   * 
   * Progress guarantees: lock-free
   * 
   * @param key key of the element to search for
   * @return iterator to an element with key equivalent to key if such element is found,
   * otherwise past-the-end iterator
   */
  iterator find(const Key& key);

  /**
   * @brief Checks if there is an element with key equivalent to key in the container.
   *
   * Progress guarantees: lock-free
   * 
   * @param key key of the element to search for
   * @return `true` if there is such an element, otherwise `false`
   */
  bool contains(const Key& key);

  /**
   * @brief Returns an iterator to the first element of the container. 
   * @return iterator to the first element 
   */
  iterator begin();

  /**
   * @brief Returns an iterator to the element following the last element of the container.
   * 
   * This element acts as a placeholder; attempting to access it results in undefined behavior. 
   * @return iterator to the element following the last element.
   */
  iterator end();

private:
  struct node;
  using concurrent_ptr = typename Reclaimer::template concurrent_ptr<node, 1>;
  using marked_ptr = typename concurrent_ptr::marked_ptr;
  using guard_ptr = typename concurrent_ptr::guard_ptr;

  struct node : Reclaimer::template enable_concurrent_ptr<node, 1>
  {
  public:
    value_type value;
  private:
    concurrent_ptr next;
    template< class... Args >
    node(Args&&... args) : value(std::forward<Args>(args)...), next() {}
    friend class harris_michael_hash_map;
  };

  struct find_info
  {
    concurrent_ptr* prev;
    marked_ptr next;
    guard_ptr cur;
    guard_ptr save;
  };

  bool find(const Key& key, std::size_t bucket, find_info& info, Backoff& backoff);

  concurrent_ptr buckets[Buckets];
};

/**
 * @brief A ForwardIterator to safely iterate the hash-map.
 * 
 * Iterators are not invalidated by concurrent insert/erase operations. However, conflicting erase
 * operations can have a negative impact on the performance when advancing the iterator, because it
 * may be necessary to rescan the bucket's list to find the next element.
 * 
 * *Note:* This iterator class does *not* provide multi-pass guarantee as `a == b` does not imply `++a == ++b`.
 * 
 * *Note:* Each iterator internally holds two `guard_ptr` instances. This has to be considered when using
 * a reclamation scheme that requires per-instance resources like `hazard_pointer` or `hazard_eras`.
 * It is therefore highly recommended to use prefix increments wherever possible.
 */
template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
class harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::iterator {
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = harris_michael_hash_map::value_type;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type*;
  using reference = value_type&;

  iterator(iterator&&) = default;
  iterator(const iterator&) = default;

  iterator& operator=(iterator&&) = default;
  iterator& operator=(const iterator&) = default;

  iterator& operator++()
  {
    assert(info.cur.get() != nullptr);
    auto next = info.cur->next.load(std::memory_order_relaxed);
    guard_ptr tmp_guard;
    // (1) - this acquire-load synchronizes-with the release-CAS (8, 9, 10, 11, 13, 15)
    if (next.mark() == 0 && tmp_guard.acquire_if_equal(info.cur->next, next, std::memory_order_acquire))
    {
      info.prev = &info.cur->next;
      info.save = std::move(info.cur);
      info.cur = std::move(tmp_guard);
    }
    else
    {
      // cur is marked for removal
      // -> use find to remove it and get to the next node with a key >= cur->key
      Key key = info.cur->value.first;
      Backoff backoff;
      map->find(key, bucket, info, backoff);
    }
    assert(info.prev == &map->buckets[bucket] ||
           info.cur.get() == nullptr ||
           (info.save.get() != nullptr && &info.save->next == info.prev));

    if (!info.cur)
      move_to_next_bucket();

    return *this;
  }
  iterator operator++(int)
  {
    iterator retval = *this;
    ++(*this);
    return retval;
  }
  bool operator==(const iterator& other) const { return info.cur.get() == other.info.cur.get(); }
  bool operator!=(const iterator& other) const { return !(*this == other); }
  reference operator*() const noexcept { return info.cur->value; }
  pointer operator->() const noexcept { return &info.cur->value; }

  void reset() {
    bucket = Buckets;
    info.prev = nullptr;
    info.cur.reset();
    info.save.reset();
  }

private:
  friend harris_michael_hash_map;

  explicit iterator(harris_michael_hash_map* map) :
    map(map),
    bucket(Buckets)
  {}

  explicit iterator(harris_michael_hash_map* map, std::size_t bucket) :
    map(map),
    bucket(bucket)
  {
    info.prev = &map->buckets[bucket];
    // (2) - this acquire-load synchronizes-with the release-CAS (8, 9, 10, 11, 13, 15)
    info.cur.acquire(*info.prev, std::memory_order_acquire);

    if (!info.cur)
      move_to_next_bucket();
  }

  explicit iterator(harris_michael_hash_map* map, std::size_t bucket, find_info&& info) :
    map(map),
    bucket(bucket),
    info(std::move(info))
  {}

  void move_to_next_bucket() {
    info.save.reset();
    while (!info.cur && bucket < Buckets - 1) {
      ++bucket;
      info.prev = &map->buckets[bucket];
      // (3) - this acquire-load synchronizes-with the release-CAS (8, 9, 10, 11, 13, 15)
      info.cur.acquire(*info.prev, std::memory_order_acquire);
    }
  }

  harris_michael_hash_map* map;
  std::size_t bucket;
  find_info info;
};

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::~harris_michael_hash_map()
{
  for (size_t i = 0; i < Buckets; ++i)
  {
    // (4) - this acquire-load synchronizes-with the release-CAS (8, 9, 10, 11, 13, 15)
    auto p = buckets[i].load(std::memory_order_acquire);
    while (p)
    {
      // (5) - this acquire-load synchronizes-with the release-CAS (8, 9, 10, 11, 13, 15)
      auto next = p->next.load(std::memory_order_acquire);
      delete p.get();
      p = next;
    }
  }
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
bool harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::find(const Key& key, std::size_t bucket,
  find_info& info, Backoff& backoff)
{
  auto& head = buckets[bucket];
  assert((info.save == nullptr && info.prev == &head) || &info.save->next == info.prev);
  concurrent_ptr* start = info.prev;
  guard_ptr start_guard = info.save; // we have to keep a guard_ptr to prevent start's node from getting reclaimed.
retry:
  info.prev = start;
  info.save = start_guard;
  info.next = info.prev->load(std::memory_order_relaxed);
  if (info.next.mark() != 0) {
    // our start node is marked for removal -> we have to restart from head
    start = &head;
    start_guard.reset();
    goto retry;
  }

  for (;;)
  {
    // (6) - this acquire-load synchronizes-with the release-CAS (8, 9, 10, 11, 13, 15)
    if (!info.cur.acquire_if_equal(*info.prev, info.next, std::memory_order_acquire))
      goto retry;

    if (!info.cur)
      return false;

    info.next = info.cur->next.load(std::memory_order_relaxed);
    if (info.next.mark() != 0)
    {
      // Node *cur is marked for deletion -> update the link and retire the element

      // (7) - this acquire-load synchronizes-with the release-CAS (8, 9, 10, 11, 13, 15)
      info.next = info.cur->next.load(std::memory_order_acquire).get();

      // Try to splice out node
      marked_ptr expected = info.cur.get();
      // (8) - this release-CAS synchronizes with the acquire-load (1, 2, 3, 4, 5, 6, 7)
      //       and the acquire-CAS (12, 14)
      //       it is the head of a potential release sequence containing (12, 14)
      if (!info.prev->compare_exchange_weak(expected, info.next,
                                            std::memory_order_release,
                                            std::memory_order_relaxed))
      {
        backoff();
        goto retry;
      }
      info.cur.reclaim();
    }
    else
    {
      if (info.prev->load(std::memory_order_relaxed) != info.cur.get())
        goto retry; // cur might be cut from the hash_map.

      Key ckey = info.cur->value.first;
      if (ckey >= key)
        return ckey == key;

      info.prev = &info.cur->next;
      std::swap(info.save, info.cur);
    }
  }
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
bool harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::contains(const Key& key)
{
  auto bucket = std::hash<Key>{}(key) % Buckets;
  find_info info{&buckets[bucket]};
  Backoff backoff;
  return find(key, bucket, info, backoff);
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
auto harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::find(const Key& key) -> iterator
{
  auto bucket = std::hash<Key>{}(key) % Buckets;
  find_info info{&buckets[bucket]};
  Backoff backoff;
  if (find(key, bucket, info, backoff))
    return iterator(this, bucket, std::move(info));
  return end();
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
template <class... Args>
bool harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::emplace(Args&&... args)
{
  auto result = emplace_or_get(std::forward<Args>(args)...);
  return result.second;
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
template <class... Args>
auto harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::get_or_emplace(Key key, Args&&... args)
-> std::pair<iterator, bool>
{
  node* n = nullptr;
  auto bucket = std::hash<Key>{}(key) % Buckets;

  const Key* pkey = &key;
  find_info info{&buckets[bucket]};
  Backoff backoff;
  for (;;)
  {
    if (find(*pkey, bucket, info, backoff))
    {
      delete n;
      return {iterator(this, bucket, std::move(info)), false};
    }
    if (n == nullptr) {
      n = new node(std::piecewise_construct,
        std::forward_as_tuple(std::move(key)),
        std::forward_as_tuple(std::forward<Args>(args)...));
      pkey = &n->value.first;
    }

    // Try to install new node
    marked_ptr cur = info.cur.get();
    info.cur.reset();
    info.cur = guard_ptr(n);
    n->next.store(cur, std::memory_order_relaxed);

    // (9) - this release-CAS synchronizes with the acquire-load (1, 2, 3, 4, 5, 6, 7)
    //       and the acquire-CAS (12, 14)
    //       it is the head of a potential release sequence containing (12, 14)
    if (info.prev->compare_exchange_weak(cur, n,
                                         std::memory_order_release,
                                         std::memory_order_relaxed))
      return {iterator(this, bucket, std::move(info)), true};

    backoff();
  }
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
template <typename Factory>
auto harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::get_or_emplace_lazy(Key key, Factory value_factory)
  -> std::pair<iterator, bool>
{
  node* n = nullptr;
  auto bucket = std::hash<Key>{}(key) % Buckets;

  const Key* pkey = &key;
  find_info info{&buckets[bucket]};
  Backoff backoff;
  for (;;)
  {
    if (find(*pkey, bucket, info, backoff))
    {
      delete n;
      return {iterator(this, bucket, std::move(info)), false};
    }
    if (n == nullptr) {
      n = new node(std::move(key), value_factory());
      pkey = &n->value.first;
    }

    // Try to install new node
    marked_ptr cur = info.cur.get();
    info.cur.reset();
    info.cur = guard_ptr(n);
    n->next.store(cur, std::memory_order_relaxed);

    // (10) - this release-CAS synchronizes with the acquire-load (1, 2, 3, 4, 5, 6, 7)
    //        and the acquire-CAS (12, 14)
    //        it is the head of a potential release sequence containing (12, 14)
    if (info.prev->compare_exchange_weak(cur, n,
                                         std::memory_order_release,
                                         std::memory_order_relaxed))
      return {iterator(this, bucket, std::move(info)), true};

    backoff();
  }
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
template <class... Args>
auto harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::emplace_or_get(Args&&... args)
  -> std::pair<iterator, bool>
{
  node* n = new node(std::forward<Args>(args)...);
  auto bucket = std::hash<Key>{}(n->value.first) % Buckets;

  find_info info{&buckets[bucket]};
  Backoff backoff;
  for (;;)
  {
    if (find(n->value.first, bucket, info, backoff))
    {
      delete n;
      return {iterator(this, bucket, std::move(info)), false};
    }
    // Try to install new node
    marked_ptr cur = info.cur.get();
    info.cur.reset();
    info.cur = guard_ptr(n);
    n->next.store(cur, std::memory_order_relaxed);

    // (11) - this release-CAS synchronizes with the acquire-load (1, 2, 3, 4, 5, 6, 7)
    //        and the acquire-CAS (12, 14)
    //        it is the head of a potential release sequence containing (12, 14)
    if (info.prev->compare_exchange_weak(cur, n,
                                         std::memory_order_release,
                                         std::memory_order_relaxed))
      return {iterator(this, bucket, std::move(info)), true};

    backoff();
  }
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
bool harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::erase(const Key& key)
{
  auto bucket = std::hash<Key>{}(key) % Buckets;
  Backoff backoff;
  find_info info{&buckets[bucket]};
  // Find node in hash_map with matching key and mark it for erasure.
  do
  {
    if (!find(key, bucket, info, backoff))
      return false; // No such node in the hash_map
    // (12) - this acquire-CAS synchronizes with the release-CAS (8, 9, 10, 11, 13, 15)
    //        and is part of a release sequence headed by those operations
  } while (!info.cur->next.compare_exchange_weak(info.next,
                                                 marked_ptr(info.next.get(), 1),
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed));

  assert(info.next.mark() == 0);
  assert(info.cur.mark() == 0);

  // Try to splice out node
  marked_ptr expected = info.cur;
  // (13) - this release-CAS synchronizes with the acquire-load (1, 2, 3, 4, 5, 6, 7)
  //        and the acquire-CAS (12, 14)
  //        it is the head of a potential release sequence containing (12, 14)
  if (info.prev->compare_exchange_weak(expected, info.next.get(),
                                       std::memory_order_release,
                                       std::memory_order_relaxed))
    info.cur.reclaim();
  else
    // Another thread interfered -> rewalk the bucket's list to ensure
    // reclamation of marked node before returning.
    find(key, bucket, info, backoff);
   
  return true;
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
auto harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::erase(iterator pos) -> iterator
{
  Backoff backoff;
  auto next = pos.info.cur->next.load(std::memory_order_relaxed);
  while (next.mark() == 0)
  {
    // (14) - this acquire-CAS synchronizes with the release-CAS (8, 9, 10, 11, 13, 15)
    //        and is part of a release sequence headed by those operations
    if (pos.info.cur->next.compare_exchange_weak(next,
                                                 marked_ptr(next.get(), 1),
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed))
      break;

    backoff();
  }

  guard_ptr next_guard(next.get());
  assert(pos.info.cur.mark() == 0);

  // Try to splice out node
  marked_ptr expected = pos.info.cur;
  // (15) - this release-CAS synchronizes with the acquire-load (1, 2, 3, 4, 5, 6, 7)
  //        and the acquire-CAS (12, 14)
  //        it is the head of a potential release sequence containing (12, 14)
  if (pos.info.prev->compare_exchange_weak(expected, next_guard,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
    pos.info.cur.reclaim();
    pos.info.cur = std::move(next_guard);
  } else {
    next_guard.reset();
    Key key = pos.info.cur->value.first;

    // Another thread interfered -> rewalk the list to ensure reclamation of marked node before returning.
    find(key, pos.bucket, pos.info, backoff);
  }

  if (!pos.info.cur)
    pos.move_to_next_bucket();

  return pos;
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
auto harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::begin() -> iterator
{
  return iterator(this, 0);
}

template <class Key, class Value,class Reclaimer, size_t Buckets, class Backoff>
auto harris_michael_hash_map<Key, Value, Reclaimer, Buckets, Backoff>::end() -> iterator
{
  return iterator(this);
}

}

#endif