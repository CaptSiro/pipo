// T is bound by the field value: unbox gives a string here
import io
<T>type Box = { T value }
<T>function unbox(Box<T> b) -> T {
    return b:value
}
const int z = unbox({ value: "s" })
