// a tagged alias whose functions live here, found by dot calls on deeper tags declared elsewhere
export type Handle = int

export function raw(Handle h) -> int {
    return h
}
