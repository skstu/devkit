package org.skstu.libflui;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.UUID;

/** Imports a selected provider stream without trusting its size or loading it into memory. */
public final class FluiFileImport {
    private FluiFileImport() {}

    public static File copy(InputStream source, File root, String displayName) throws IOException {
        File directory = new File(root, UUID.randomUUID().toString());
        File partial = new File(directory, ".partial");
        File target = new File(directory, safeName(displayName));
        boolean complete = false;
        boolean owned = false;
        try {
            try (InputStream input = source) {
                if (!directory.mkdirs()) throw new IOException("Cannot create selection directory");
                owned = true;
                try (FileOutputStream output = new FileOutputStream(partial)) {
                    byte[] buffer = new byte[65536];
                    int count;
                    while ((count = input.read(buffer)) != -1) {
                        if (count == 0) {
                            int single = input.read();
                            if (single == -1) break;
                            output.write(single);
                        } else output.write(buffer, 0, count);
                    }
                }
            }
            if (!partial.renameTo(target)) throw new IOException("Cannot finish selected file");
            complete = true;
            return target;
        } finally {
            if (!complete && owned) {
                partial.delete();
                target.delete();
                directory.delete();
            }
        }
    }

    private static String safeName(String value) {
        String name = value == null ? "" : value.replace('\\', '/');
        name = name.substring(name.lastIndexOf('/') + 1).replaceAll("[\\p{Cntrl}]", "_");
        if (name.isEmpty() || name.equals(".") || name.equals("..") || name.equals(".partial"))
            return "selected-file";
        return name;
    }
}
