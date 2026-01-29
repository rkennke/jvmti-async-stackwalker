import java.util.Random;

public class HelloWorld {
    static final int NUM_ELEMENTS = 2000000;
    static final Random rng = new Random();
    static int dummy;
    static int array[] = new int[NUM_ELEMENTS];
    static void run() throws Exception {
	while (true) {
	    dummy = dummy + array[Math.abs(rng.nextInt() % NUM_ELEMENTS)];
	}
    }

    public static void main(String[] args) throws Exception {
	System.out.println("Filling memory with random numbers");
	for (int i = 0; i < NUM_ELEMENTS; i++) {
	    array[i] = rng.nextInt();
	}
	System.out.println("Randomly accessing the memory");
	run();
    }
}
