# CS 562 JVM

This JVM is written for the virtual machines course at IIT.

To build the JVM, clone it to your computer and run `cmake ./` from within the cloned project. Then run `make`. It will create an executable called `jvm` which can be run. Running it with no arguments will display all the options you can pass.

## Current status

Currently this JVM is not fully compliant to the java specifications and will not run any class files.

### Unimplemented features
* Garbage Collection is only partially implemented
* Method resolution is not yet implemented
* Conversion of UTF-8 to UTF-16
    * Currently UTF-8 is treated as a valid UTF-16
* Exceptions are not yet handled by the interpreter loop
* Only primitive and String constants are supported for the LDC instruction
* Exceptions thrown by instructions other than athrow are not properly initialized
    * Their message is initialized by finding and setting the first field which stores an instance of java/lang/String. This should be switched to calling the constructor of the exception
* Monitors are not currently exited during exceptional returns from a method
* Threads cannot currently be created even though the framework for them exists

### Bugs
* There might be a possibility for objects to be unintentially garbage collected during class initialization.  
    * This needs to be looked into further