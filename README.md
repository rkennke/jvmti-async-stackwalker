
# JVMTI stack-walking example

This code is meant to demonstrate the functionality of the new 'asynchronous' (aka unbiased)
JVMTI stack-walker that is developed in [JDK-8373578](https://bugs.openjdk.org/browse/JDK-8373578).
The following instructions require a JDK that is built from
[JDK-8373578-v2](https://github.com/rkennke/jdk/tree/JDK-8373578-v2).

## Building the agent

```
gcc -g -I$JAVA_HOME/include -I$JAVA_HOME/include/linux -shared -o agent.so -fPIC agent.c
```

## Building the example

```
javac HelloWorld.java
```

## Running the example

```
$JAVA_HOME/bin/java -XX:+FlightRecorder -XX:StartFlightRecording=filename=test.jfr,dumponexit=true  -agentpath:./agent.so HelloWorld
```

Let the program run for a while, until a couple of dots have been printed. The stack-traces will
be recorded in the resulting `test.jfr` file - watch out for `AsyncStackTrace` events.
