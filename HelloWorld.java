public class HelloWorld {
    static int dummy;
    static void run() throws Exception {
	while (true) {
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
