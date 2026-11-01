import io
import os
import "./ui"

type Item = {
    string name,
    float cost,
}

function view(Item item) -> string {
    return item:name + " " + str(item:cost) + ",-"
}

impl ui:View for Item

type Order = Item[]

function total(Order order) -> float {
    return order.reduce((a, x) -> a + x:cost, 0)
}

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

enum string MenuOption {
    EXIT = "Exit",
    ADD = "Add",
    REMOVE = "Remove",
    CASHOUT = "Cashout and print the receipt",
    VIEW_ORDER = "View order",
}

function view(MenuOption option) -> string {
    return option.enum_value()
}

impl ui:View for MenuOption

const main_menu = [
    MenuOption:ADD,
    MenuOption:REMOVE,
    MenuOption:VIEW_ORDER,
    MenuOption:CASHOUT,
    MenuOption:EXIT
]

let Order order = []

function str(Order order) -> string {
    let sb = "---- ORDER ----" + io:nl

    for i in ...#order {
        sb += "[" + (i + 1).str() + "] " + order[i].view() + io:nl
    }

    sb += "Total: " + order.total().str() + io:nl
    return sb
}

while true {
    io:print("---- MAIN MENU ----")
    match main_menu[main_menu.ui:select()] {
        MenuOption:ADD -> {
            const item = items[items.ui:select()]
            order.push(item)
        }

        MenuOption:REMOVE -> {
            const index = items.ui:select()
            order.remove(index)
        }

        MenuOption:VIEW_ORDER -> {
            io:print(order.str())
        }

        MenuOption:CASHOUT -> {
            io:print("Writing to file: './order.txt'")
            match io:write_file(os:join_pp('./order.txt'), order.str()) {
                success(bytes_written) -> io:print("Wrote " + bytes_written.str() + " B")
                fail(message) -> io:eprint(message)
            }
        }

        MenuOption:EXIT -> {
            break;
        }
    }
}
