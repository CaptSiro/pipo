import math

// 1e308 + 1e308 does not fit in a float, even though the total (1e308) would
math:sum_precise([1e308, 1e308, -1e308])
