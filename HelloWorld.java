public class HelloWorld {
    static int dummy;
    static void run() throws Exception {
	long end = System.currentTimeMillis() + 20000;
	while (System.currentTimeMillis() < end) {
	    long start = System.currentTimeMillis();
	    while (System.currentTimeMillis() - start < 1000) {
		// Busy loop.
		dummy = dummy + dummy * 2;
	    }
	    System.out.println(".");
	}
    }

    public static void main(String[] args) throws Exception {
	run();
    }
}
