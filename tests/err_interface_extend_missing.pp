import "./modules/ordering" -> ord

type Point = { int x }
function <(Point a, Point b) -> bool { return a:x < b:x }
function <=(Point a, Point b) -> bool { return a:x <= b:x }
// Comparator extends Equals, which needs ==
impl ord:Comparator for Point
