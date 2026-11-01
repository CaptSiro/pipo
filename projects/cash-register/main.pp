import io

type Item = {
    string name,
    float cost,
}

type Order = Item[]

const Item[] items = [
    {
        name: "Burger",
        cost: 129.9
    },
    {
        name: "Cola 0.3 l",
        cost: 39.9
    }
]

enum string Option {
    EXIT = "Exit",
    ADD = "Add",
    REMOVE = "Remove",
    CASHOUT = "Cashout and print the receipt",
}



function choose(Option[] options) -> Option {
    while true {
        for i in ...#options {
            io:print("[" + (i + 1).str() + "]: " + options[i].enum_value().str())
        }

        if some(choice) = io:input("Choice: ").parse_int() {
            if 1 <= choice && choice <= #options {
                return options[choice - 1]
            } else {
                io:print(io:nl + "Choice is out of bounds")
            }
        } else {
            io:print(io:nl + "Choice is not a number" + io:nl)
        }
    }
}



const main_menu = [
    Option:ADD,
    Option:REMOVE,
    Option:CASHOUT,
    Option:EXIT
]

const option = main_menu.choose()

io:print(option)
