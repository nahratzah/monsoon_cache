[![Build Status](https://travis-ci.org/nahratzah/monsoon_cache.svg?branch=master)](https://travis-ci.org/nahratzah/monsoon_cache)

# monsoon cache

Monsoon-cache is a functional, in-memory cache, in C++.

## Requirements

C++17, although you'll probably get by with anything higher than C++14 (clang-4.0.0 with -std=c++1z works fine for me).

## Usage

A cache is defined by a number of components:
- a key type
- a mapped type
- a hash function
- an equality function
- an allocator
- a create function

A cache is instantiated by creating and configuring a builder, after which the ``build()`` method creates the cache.

### Example

    #include <monsoon/cache/cache.h> // Interfaces
    #include <monsoon/cache/impl.h> // To enable the build() method of cache_builder.
    
    using monsoon::cache::cache;
    
    cache<int, int> c = cache<int, int>::builder()
        .build(
            // Create function.
            [](int i) {
              return 2 * i;
            });

In this example, the cache uses a *key type* of ``int`` (the first template argument).
It maps these to a *mapped type* of ``int`` (the second template argument).

The *hash function* defaults to ``std::hash<key_type>``,
while the *equality function* defaults to ``std::equal_to<key_type>``.
The *allocator* defaults ``std::allocator<int>``.

In order to use the cache, the ``get`` method or the function operator can be used:

    std::shared_ptr<int> v = c.get(21); // Using get() method.
    std::shared_ptr<int> u = c(21);     // Using function operator.

The cache returns shared pointers to the values it contains.
While any reference to the shared pointer remains valid, the cache will always
return the same shared pointer.
If the value in the cache should not be modified, use a ``const`` qualifier on
the value type:

    cache<int, const int> c = cache<int, const int>::builder()
        .build(
            // Create function.
            [](int i) {
              return 2 * i;
            });

(The reason to default to mutable values, is that this allows mutable data types
to be shared in the cache.)

## Linking

This is a header-only library.

To use it, you can simple supply ``-I/path/to/monsoon-cache/include`` to the
compiler.

If you're using CMake, you can instead import the library using (for instance)
a git submodule:

    add_subdirectory(path/to/monsoon-cache)
    target_link_libraries(my_target monsoon-cache)

If monsoon-cache is properly installed, the following should work:

    find_package(monsoon-cache 0.0 REQUIRED)
    target_link_libraries(my_target monsoon-cache)

(monsoon-cache is an interface library, so it does not have an actual library,
but does contain options to link correctly (thread-safe) and get the correct
includes via the target.)
In both cases, the include directories are set correctly.

## Tweaking

The behaviour of the cache can be altered by methods on the builder.
To create a cache that caches 100,000 elements, for example:

    cache<int, int>::builder()
        .max_size(100 * 1000)
        .build();

I recommend invoking the builder in a separate .cc file, to minimize time spent
recompiling when tweaking cache parameters.

## Extended Cache

The cache supports a multi-argument lookup, via the extended cache interface.

    #include <monsoon/cache/cache.h>
    #include <monsoon/cache/impl.h> // To enable the build() method of cache_builder.

    using my_cache_type = extended_cache<int, int, HashFn, EqualityFn, Allocator, CreateFn>;

    my_cache_type ec = my_cache_type::builder()
        .build(CreateFn());

The extended cache allows any argument combination that is supported by
the ``HashFn``, ``EqualityFn`` and ``CreateFn`` functions.

An ``extended_cache`` is implicitly convertible to a (not extended) ``cache``.

### Example

    std::shared_ptr<int> v = ec(1, 2, 3);

In this case, the hash function will be invoked as:

    HashFn hash_fn;
    hash_fn(1, 2, 3); // passed as const references

While the EqualityFn will be invoked as:

    EqualityFn eq_fn;
    eq_fn(key,      // key of a cache element.
          1, 2, 3); // passed as const references.

In the case of a cache miss, the mapped value will be constructed using the CreateFn:

    CreateFn cfn;
    cfn(1, 2, 3); // passed using std::forward (perfect forwarding).

while the key of the cache will be constructed as:

    key_type(1, 2, 3); // passed as const references.

(The key\_type is constructed prior to the invocation of CreateFn.)

## Identity Cache

Instead of using an external key\_type, an identity cache can be used.

An identity cache is created by supplying ``void`` as the first (key) argument
to the cache.

An identity cache will use the mapped type as its key type.

### Example

    // A document that lazily loads html pages using a URL.
    // We use the URL as key into the cache, therefore we choose
    // to use an identity cache, in order not to store the key twice.
    class HtmlDocument {
     public:
      HtmlDocument(std::string url) : url(url) {}

      std::string load() {
        // Thread-safe code omitted for clarity.

        if (doc_.empty())
          doc_ = doHttpCall(url);
        return doc_;
      }

     public:
      const std::string url;

     private:
      std::string doc_;
    };

    struct HtmlDocumentHash {
      // Url hash.
      std::size_t operator()(const std::string& url) const {
        return std::hash<std::string>()(url);
      }

      // Key-type based hash must be implemented, irrespective of use.
      std::size_t operator()(const HtmlDocument& doc) const {
        return std::hash<std::string>()(doc.url);
      }
    };

    struct HtmlDocumentEq {
      // Url equality.
      bool operator()(const HtmlDocument& doc,
                      const std::string url) const {
        return doc.url == url;
      }

      // Key-type based equality must be implemented, irrespective of use.
      bool operator()(const HtmlDocument& doc,
                      const HtmlDocument& search) const {
        return doc.url == search.url;
      }
    };

    struct HtmlDocumentCreate {
      // Construct using URL.
      template<typename Alloc>
      bool operator()(Alloc alloc, std::string url) const {
        return std::allocate_shared<HtmlDocument>(alloc, std::move(url));
      }

      // Key-type based construction must be implemented, irrespective of use.
      template<typename Alloc>
      bool operator()(Alloc alloc, const HtmlDocument& doc) const {
        return std::allocate_shared<HtmlDocument>(alloc, doc);
      }
    };

    // Declare cache type.
    using DocumentCache = extended_cache<
        void,
        HtmlDocument,
        HtmlDocumentHash,
        HtmlDocumentEq,
        std::allocator<HtmlDocument>,
        HtmlDocumentCreate>;

    // Create cache.
    DocumentCache documentCache = DocumentCache::builder()
        .access_expire(std::chrono::minutes(10))
        .build();

    // Access document using cache.
    std::shared_ptr<HtmlDocument> localhostDoc =
        documentCache("http://localhost/");

In this example, the ``localhostDoc`` will cause invocations of:

    HtmlDocumentHash hash;
    hash("http://localhost/");    // const char*const& argument

    HtmlDocumentEq eq;
    eq("http://localhost/");      // const char*const& argument

    HtmlDocumentCreate create;
    create("http://localhost/");  // const char*&& argument

## Additional Documentation

Documentation can be generated by the ``monsoon_cache-doc`` target.

Or alternatively, can be [browsed online](https://www.stack.nl/~ariane/cache/).
