# fizzbuzz.cl — FizzBuzz in Care Language

var i = 1;
while (i <= 30) {
    var rem3 = i - (i / 3) * 3;
    var rem5 = i - (i / 5) * 5;
    if (rem3 == 0) {
        if (rem5 == 0) {
            print "FizzBuzz";
        } else {
            print "Fizz";
        }
    } else {
        if (rem5 == 0) {
            print "Buzz";
        } else {
            print str(i);
        }
    }
    i = i + 1;
}
