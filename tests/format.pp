import io

io:print(format("%d + %.2f = %s", 1, 2.5, "3.50"))
io:print(format("[%5d] [%-5d] [%05d] [%+d] [% d]", 42, 42, 42, 42, 42))
io:print(format("%x %X %o %#x %u", 255, 255, 8, 255, -1))
io:print(format("%.3e %g %G %f", 12345.678, 0.0001, 1e20, 3))
io:print(format("[%10.3s] [%-6s] [%s]", "abcdef", "ab", [1, 2]))
io:print(format("%c%c%c [%3c] [%-3c]", 80, "i", 0x1F600, "x", "y"))
io:print(format("[%*d] [%-*d] [%.*f]", 6, 7, 4, 8, 1, 3.14159))
io:print(format("100%% done, %s and %s", true, none()))
io:print(format("%ld %lld %hhd %Lf", 1, 2, 3, 4.5))
io:print(format("no specs"), format(""))
