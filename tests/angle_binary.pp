import io

// binary < and > stay overloadable
type Money = { int cents }
function <(Money a, Money b) -> bool {
    return a:cents < b:cents
}

function >(Money a, Money b) -> bool {
    return a:cents > b:cents
}

io:print_nl(Money{ cents: 1 } < Money{ cents: 2 }, Money{ cents: 1 } > Money{ cents: 2 })
