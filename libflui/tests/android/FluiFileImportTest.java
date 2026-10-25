package org.skstu.libflui;

import java.io.*;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.Arrays;

public final class FluiFileImportTest {
    private static final class Generated extends InputStream {
        long remaining, offset;
        boolean closed;
        final MessageDigest digest;
        Generated(long size) throws Exception { remaining=size;digest=MessageDigest.getInstance("SHA-256"); }
        public int available() { return Integer.MAX_VALUE; } // Must not size an allocation from this.
        public int read() { throw new AssertionError("Unexpected single-byte read"); }
        public int read(byte[] bytes,int start,int length) {
            if(length>65536)throw new AssertionError("Unbounded read");
            if(remaining==0)return -1;
            int count=(int)Math.min(remaining,length);
            for(int i=0;i<count;i++)bytes[start+i]=(byte)((offset+i)%251);
            digest.update(bytes,start,count);offset+=count;remaining-=count;return count;
        }
        public void close() { closed=true; }
    }
    private static void require(boolean value) { if(!value)throw new AssertionError(); }
    private static void clean(File file) {
        File[] children=file.listFiles();if(children!=null)for(File child:children)clean(child);
        file.delete();
    }
    public static void main(String[] args) throws Exception {
        File root=Files.createTempDirectory("flui-import-test-").toFile();
        try {
            if(args[0].equals("large")) {
                long size=561539152L; // Allocation size observed in both Pixel crashes.
                Generated input=new Generated(size);
                File output=FluiFileImport.copy(input,root,"review.mp4");
                require(input.closed && output.length()==size);
                MessageDigest digest=MessageDigest.getInstance("SHA-256");
                try(InputStream saved=new FileInputStream(output)) {
                    byte[] chunk=new byte[65536];int count;
                    while((count=saved.read(chunk))!=-1)digest.update(chunk,0,count);
                }
                require(Arrays.equals(input.digest.digest(),digest.digest()));
            } else if(args[0].equals("failure")) {
                for(final boolean closeFailure:new boolean[]{false,true}) {
                    final boolean[] closed={false};
                    InputStream input=new InputStream() {
                        int left=100000;
                        public int read() throws IOException { if(left--<=0)throw new IOException("provider stopped");return 1; }
                        public int read(byte[] b,int o,int n) throws IOException {
                            if(closeFailure)return -1;
                            if(left<=0)throw new IOException("provider stopped");
                            int count=Math.min(left,n);left-=count;return count;
                        }
                        public void close() throws IOException { closed[0]=true;if(closeFailure)throw new IOException("close failed"); }
                    };
                    try {FluiFileImport.copy(input,root,"partial.mp4");throw new AssertionError();}
                    catch(IOException expected) {}
                    require(closed[0] && root.list().length==0);
                }
            } else {
                for(String name:new String[]{"../../movie.mp4","..\\..\\movie.mp4",".partial","..","照片.mp4"}) {
                    File one=FluiFileImport.copy(new ByteArrayInputStream(new byte[]{1,2,3}),root,name);
                    File two=FluiFileImport.copy(new ByteArrayInputStream(new byte[0]),root,name);
                    require(one.getCanonicalPath().startsWith(root.getCanonicalPath()+File.separator));
                    require(!one.equals(two) && one.length()==3 && two.length()==0);
                    require(!new File(one.getParentFile(),".partial").exists());
                }
            }
            System.out.println(args[0]+" passed");
        } finally { clean(root); }
    }
}
