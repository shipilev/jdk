import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.lang.reflect.Method;
import java.lang.ref.Reference;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import javax.tools.JavaCompiler;
import javax.tools.ToolProvider;
import java.util.ArrayList;
import java.util.List;

public class ClassUnloadTest {

    private static final String CLASS_NAME = "DynamicClass";
    private static final String METHOD_NAME = "run";
    private static final String SOURCE_CODE =
        "public class " + CLASS_NAME + " {" +
        "   public void " + METHOD_NAME + "() {" +
        "       double sum = 0;" +
        "       for (int i = 0; i < 1_000_000; i++) {" +
        "           sum += Math.sin(i) * Math.cos(i);" +  // Expensive computation
        "       }" +
        "       System.out.println(\"Computation result: \" + sum);" +
        "   }" +
        "}";

    public static void main(String[] args) throws Exception {
        int iterations = 100000;
        Path tempDir = Files.createTempDirectory("benchmark");
        File javaFile = new File(tempDir.toFile(), CLASS_NAME + ".java");

        try (FileWriter writer = new FileWriter(javaFile)) {
            writer.write(SOURCE_CODE);
        }

        JavaCompiler compiler = ToolProvider.getSystemJavaCompiler();
        compiler.run(null, null, null, javaFile.getPath());

        Path classFile = tempDir.resolve(CLASS_NAME + ".class");
        for (int j = 0; j < iterations; j++) {
            loadAndInvoke(classFile);
        }

        // Cleanup
        Files.deleteIfExists(classFile);
        Files.deleteIfExists(javaFile.toPath());
        Files.deleteIfExists(tempDir);
    }

    private static void loadAndInvoke(Path classFile) throws Exception {
        URL classUrl = classFile.getParent().toUri().toURL();
        URLClassLoader classLoader = new URLClassLoader(new URL[]{classUrl}, null);
        System.out.println("Instantiated classloader " + classLoader);

        Class<?> dynamicClass = classLoader.loadClass(CLASS_NAME);
        Object instance = dynamicClass.getDeclaredConstructor().newInstance();
        Method method = dynamicClass.getMethod(METHOD_NAME);
        System.out.println("Loaded another class with classloader " + classLoader);

        method.invoke(instance);
        System.out.println("Invoked method with with classloader " + classLoader);

        System.out.println();
        System.out.flush();
    }
}
