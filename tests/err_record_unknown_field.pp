// a record literal may not name a field its closed record type does not have
type Props = { string label, bool? busy }
function button(Props p) -> string {
    return p:label
}
const b = <button lable="Save" label="x"/>
