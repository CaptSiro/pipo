// a type alias cannot be defined through itself: an error even when the types are unused
type A = B
type B = A
