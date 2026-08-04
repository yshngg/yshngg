# Go Slices: Introduction

Slices are a fundamental data structure in Go that provide a more powerful and flexible alternative to arrays. This guide covers the basics of slices and how to use them effectively.

## What are Slices?

A slice is a dynamically-sized, flexible view into the elements of an array. Slices are more common than arrays in Go code because they are more versatile.

## Creating Slices

### Using a slice literal

```go
// Create a slice of strings
names := []string{"Alice", "Bob", "Charlie"}

// Create a slice of integers
numbers := []int{1, 2, 3, 4, 5}
```

### Using the `make` function

```go
// Create a slice with length 5 and capacity 10
s := make([]int, 5, 10)

// Create a slice with length and capacity both 5
s := make([]int, 5)
```

### From an array

```go
arr := [5]int{1, 2, 3, 4, 5}
s := arr[1:4] // s contains [2, 3, 4]
```

## Slice Internals

A slice has three components:
- **Pointer**: Points to the underlying array
- **Length**: Number of elements in the slice
- **Capacity**: Maximum number of elements the slice can hold

```go
s := []int{2, 3, 5, 7, 11, 13}
// s has length 6 and capacity 6

s = s[:0]  // length 0, capacity 6
s = s[:4]  // length 4, capacity 6
s = s[2:]  // length 2, capacity 4
```

## Common Operations

### Appending to slices

```go
var s []int
s = append(s, 1)        // [1]
s = append(s, 2, 3, 4)  // [1, 2, 3, 4]

// Append one slice to another
s1 := []int{1, 2}
s2 := []int{3, 4}
s1 = append(s1, s2...)  // [1, 2, 3, 4]
```

### Copying slices

```go
src := []int{1, 2, 3, 4}
dst := make([]int, 2)
copy(dst, src)  // dst = [1, 2]
```

### Iterating over slices

```go
s := []string{"apple", "banana", "cherry"}

// Using range
for i, v := range s {
    fmt.Printf("Index %d: %s\n", i, v)
}

// Using traditional for loop
for i := 0; i < len(s); i++ {
    fmt.Printf("Index %d: %s\n", i, s[i])
}
```

## Important Concepts

### Nil slices

```go
var s []int  // nil slice
fmt.Println(s == nil)  // true
fmt.Println(len(s))    // 0
fmt.Println(cap(s))    // 0
```

### Empty slices

```go
s := []int{}           // empty slice
s := make([]int, 0)    // empty slice
fmt.Println(s == nil)  // false
fmt.Println(len(s))    // 0
```

### Slice expressions

```go
s := []int{0, 1, 2, 3, 4, 5}

s[2:5]   // [2, 3, 4]
s[:3]    // [0, 1, 2]
s[3:]    // [3, 4, 5]
s[:]     // [0, 1, 2, 3, 4, 5] (copy of s)
```

## Best Practices

1. **Prefer slices over arrays** for most use cases
2. **Use `make`** when you know the required capacity
3. **Be careful with slice sharing** - modifying a slice can affect other slices that share the same underlying array
4. **Use `copy()`** when you need an independent copy
5. **Consider capacity** when performance matters

## Common Pitfalls

### Modifying shared underlying arrays

```go
s1 := []int{1, 2, 3, 4}
s2 := s1[1:3]  // s2 = [2, 3]

s2[0] = 99     // This also modifies s1!
// s1 is now [1, 99, 3, 4]
```

### Append and capacity growth

```go
s := []int{1, 2, 3}
fmt.Printf("len=%d cap=%d %v\n", len(s), cap(s), s)
// Output: len=3 cap=3 [1, 2, 3]

s = append(s, 4)
fmt.Printf("len=%d cap=%d %v\n", len(s), cap(s), s)
// Output: len=4 cap=6 [1, 2, 3, 4] (capacity doubled)
```

## Performance Tips

- **Pre-allocate capacity** with `make` when you know the final size
- **Reuse slices** when possible to reduce allocations
- **Use `copy()`** instead of append loops for better performance
- **Consider the growth strategy** - Go typically doubles capacity when needed

## Conclusion

Slices are a powerful feature in Go that provide dynamic, flexible arrays. Understanding how slices work internally and their common operations is essential for writing efficient Go code.