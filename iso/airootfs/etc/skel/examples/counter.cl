# counter.cl — Function example in Care Language

func countdown(n) {
    while (n > 0) {
        print str(n) + "...";
        n = n - 1;
    }
    print "Go!";
}

func add(a, b) {
    return a + b;
}

countdown(5);

var result = add(10, 32);
print "10 + 32 = " + str(result);
