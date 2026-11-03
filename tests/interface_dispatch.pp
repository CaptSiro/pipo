// `impl` records the functions it found on the type, so a module that only
// knows the interface can call them, even though they are not exported
import io
import "./modules/views" -> views

enum string MenuOption {
    ADD = "Add",
    EXIT = "Exit",
}

function view(MenuOption o) -> string {
    return o.enum_value()
}

impl views:View for MenuOption

type Item = { string name, float cost }

function view(Item item) -> string {
    return item:name + " (" + item:cost.str() + ")"
}

impl views:View for Item

io:print_nl(views:render([MenuOption:ADD, MenuOption:EXIT]))
io:print_nl(views:render_one(MenuOption:EXIT))
io:print_nl(views:render_one(Item{ name: "Burger", cost: 129.9 }))

// a mixed array of implementing types dispatches per element
const Item cola = { name: "Cola", cost: 39.9 }
io:print_nl(views:render([MenuOption:ADD, cola]))

// a typed declaration tags the records inside an array, directly or through an alias
const Item[] menu = [{ name: "Burger", cost: 129.9 }, { name: "Fries", cost: 49.9 }]
io:print_nl(views:render(menu))
type Order = Item[]
let Order order = [{ name: "Cola", cost: 39.9 }]
io:print_nl(views:render(order))
