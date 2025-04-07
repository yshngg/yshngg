# What's in a name? (Go Names Talk 2014)

_**Author:** Andrew Gerrand (Google Inc.)_  
_**Date:** October 2014_

### Names matter

Readability is the defining quality of good code.

Good names are critical to readability.

This talk is about naming in Go.

### Good names

A good name is:

- Consistent (easy to guess),
- Short (easy to type),
- Accurate (easy to understand).

### A rule of thumb

The greater the distance between a name's declaration and its uses, the longer the name should be.

### Use MixedCase

Names in Go should use `MixedCase`.

(Don't use `names_with_underscores`.)

Acronyms should be all capitals, as in `ServeHTTP` and `IDProcessor`.

### Local variables

Keep them short; long names obscure what the code _does_.

Common variable/type combinations may use really short names:

Prefer `i` to `index`.

Prefer `r` to `reader`.

Prefer `b` to `buffer`.

Avoid redundant names, given their context:

Prefer `count` to `runeCount` inside a function named `RuneCount`.

Prefer `ok` to `keyInMap` in the statement

```go
v, ok := m[k]
```

Longer names may help in long functions, or functions with many local variables. (But often this just means you should refactor.)

### Bad

```go
func RuneCount(buffer []byte) int {
    runeCount := 0
    for index := 0; index < len(buffer); {
        if buffer[index] < RuneSelf {
            index++
        } else {
            _, size := DecodeRune(buffer[index:])
            index += size
        }
        runeCount++
    }
    return runeCount
}
```

### Good

```go
func RuneCount(b []byte) int {
    count := 0
    for i := 0; i < len(b); {
        if b[i] < RuneSelf {
            i++
        } else {
            _, n := DecodeRune(b[i:])
            i += n
        }
        count++
    }
    return count
}
```

### Parameters

Function parameters are like local variables, but they also serve as documentation.

Where the types are descriptive, they should be short:

```go
func AfterFunc(d Duration, f func()) *Timer

func Escape(w io.Writer, s []byte)
```

Where the types are more ambiguous, the names may provide documentation:

```go
func Unix(sec, nsec int64) Time

func HasPrefix(s, prefix []byte) bool
```

### Return values

Return values on exported functions should only be named for documentation purposes.

These are good examples of named return values:

```go
func Copy(dst Writer, src Reader) (written int64, err error)

func ScanBytes(data []byte, atEOF bool) (advance int, token []byte, err error)
```

### Receivers

Receivers are a special kind of argument.

By convention, they are one or two characters that reflect the receiver type, because they typically appear on almost every line:

```go
func (b *Buffer) Read(p []byte) (n int, err error)

func (sh serverHandler) ServeHTTP(rw ResponseWriter, req *Request)

func (r Rectangle) Size() Point
```

Receiver names should be consistent across a type's methods. (Don't use `r` in one method and `rdr` in another.)

### Exported package-level names

Exported names are qualified by their package names.

Remember this when naming exported variables, constants, functions, and types.

That's why we have `bytes.Buffer` and `strings.Reader`, not `bytes.ByteBuffer` and `strings.StringReader`.

### Interface Types

Interfaces that specify just one method are usually just that function name with 'er' appended to it.

```go
type Reader interface {
    Read(p []byte) (n int, err error)
}
```

Sometimes the result isn't correct English, but we do it anyway:

```go
type Execer interface {
    Exec(query string, args []Value) (Result, error)
}
```

Sometimes we use English to make it nicer:

```go
type ByteReader interface {
    ReadByte() (c byte, err error)
}
```

When an interface includes multiple methods, choose a name that accurately describes its purpose (examples: `net.Conn`, `http.ResponseWriter`, `io.ReadWriter`).

### Errors

Error types should be of the form `FooError`:

```go
type ExitError struct {
    ...
}
```

Error values should be of the form `ErrFoo`:

```go
var ErrFormat = errors.New("image: unknown format")
```

### Packages

Choose package names that lend meaning to the names they export.

Steer clear of `util`, `common`, and the like.

### Import paths

The last component of a package path should be the same as the package name.

```go
"compress/gzip" // package gzip
```

Avoid stutter in repository and package paths:

```go
"code.google.com/p/goauth2/oauth2" // bad; my fault
```

For libraries, it often works to put the package code in the repo root:

```go
"github.com/golang/oauth2" // package oauth2
```

Also avoid upper case letters (not all file systems are case sensitive).

### The standard library

Many examples in this talk are from the standard library.

The standard library is a great place to find good Go code. Look to it for inspiration.

But be warned: When the standard library was written, we were still learning. Most of it we got right, but we made some mistakes.

### Conclusion

Use short names.

Think about context.

Use your judgment.

### Thank you

_Andrew Gerrand_  
_Google Inc._  
_[adg@golang.org](mailto:adg@golang.org)_  
_[@enneff](http://twitter.com/enneff)_  
_[https://go.dev/](/)_
