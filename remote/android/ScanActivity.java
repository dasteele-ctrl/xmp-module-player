package org.modulez.remote.preview;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.ImageFormat;
import android.hardware.Camera;
import android.os.Bundle;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.FrameLayout;
import android.widget.TextView;

import com.google.zxing.BinaryBitmap;
import com.google.zxing.PlanarYUVLuminanceSource;
import com.google.zxing.ReaderException;
import com.google.zxing.common.HybridBinarizer;
import com.google.zxing.qrcode.QRCodeReader;

/** Local-only scanner. Permission and the camera live only on this screen. */
@SuppressWarnings("deprecation")
public final class ScanActivity extends Activity implements SurfaceHolder.Callback {
    private FrameLayout frame;
    private SurfaceView preview;
    private TextView hint;
    private Camera camera;
    private boolean resumed, surfaceReady;
    private final QRCodeReader reader = new QRCodeReader();

    static String[] parsePairing(String text) {
        if (text == null || text.length() > 100) return null;
        String[] fields = text.split("\\|", -1);
        if (fields.length != 4 || !fields[0].equals("MODULEZ1") ||
                !fields[2].matches("[0-9a-f]{64}") || !fields[3].matches("[0-9a-f]{6}")) return null;
        String[] octets = fields[1].split("\\.", -1);
        if (octets.length != 4) return null;
        for (String octet : octets) {
            if (!octet.matches("0|[1-9][0-9]{0,2}") || Integer.parseInt(octet) > 255) return null;
        }
        return fields;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        frame = new FrameLayout(this);
        preview = new SurfaceView(this);
        frame.addView(preview, new FrameLayout.LayoutParams(-1,-1,Gravity.CENTER));
        hint = new TextView(this);
        hint.setText("Point the camera at the QR code in Modulez Player");
        hint.setGravity(Gravity.CENTER);
        hint.setPadding(16,24,16,24);
        hint.setBackgroundColor(0xdb171b1d);
        hint.setTextColor(0xffeeeeee);
        frame.addView(hint, new FrameLayout.LayoutParams(-1,-2,Gravity.CENTER_HORIZONTAL | Gravity.CENTER_VERTICAL));
        setContentView(frame);
        // Put the instruction outside the scan area, below system bars.
        hint.setTranslationY(-getResources().getDisplayMetrics().heightPixels * .34f);
        preview.getHolder().addCallback(this);
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[]{Manifest.permission.CAMERA}, 1);
    }

    @Override public void onRequestPermissionsResult(int code,String[] names,int[] grants) {
        super.onRequestPermissionsResult(code,names,grants);
        if (code!=1) return;
        if (grants.length==0 || grants[0]!=PackageManager.PERMISSION_GRANTED) {
            manual("Camera access is needed to scan. You can pair using the address and code.");
        } else startCamera();
    }

    private void manual(String message) {
        setResult(RESULT_CANCELED,new Intent().putExtra("message",message));
        finish();
    }

    @Override protected void onResume() { super.onResume(); resumed=true; startCamera(); }
    @Override protected void onPause() { resumed=false; closeCamera(); super.onPause(); }
    @Override public void surfaceCreated(SurfaceHolder holder) { surfaceReady=true; startCamera(); }
    @Override public void surfaceChanged(SurfaceHolder h,int fmt,int width,int height) { startCamera(); }
    @Override public void surfaceDestroyed(SurfaceHolder holder) { surfaceReady=false; closeCamera(); }

    private void closeCamera() {
        if (camera!=null) {
            camera.setOneShotPreviewCallback(null);
            camera.release();
            camera=null;
        }
    }

    private void startCamera() {
        if (!resumed || !surfaceReady || camera!=null || isFinishing() ||
                checkSelfPermission(Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED) return;
        try {
            int count=Camera.getNumberOfCameras();
            if (count==0) { manual("No camera available. Enter the pairing details instead."); return; }
            Camera.CameraInfo info = new Camera.CameraInfo();
            int id=0;
            for (int i=0;i<count;i++) {
                Camera.getCameraInfo(i,info);
                if (info.facing==Camera.CameraInfo.CAMERA_FACING_BACK) { id=i; break; }
            }
            Camera.getCameraInfo(id,info);
            camera=Camera.open(id);
            Camera.Parameters settings=camera.getParameters();
            Camera.Size best=settings.getPreviewSize();
            // Aim for 720p luminance frames. Large sensor frames slow QR decoding.
            for (Camera.Size size:settings.getSupportedPreviewSizes())
                if (Math.abs(size.width*size.height-921600)<Math.abs(best.width*best.height-921600)) best=size;
            settings.setPreviewSize(best.width,best.height);
            settings.setPreviewFormat(ImageFormat.NV21);
            if (settings.getSupportedFocusModes()!=null &&
                    settings.getSupportedFocusModes().contains(Camera.Parameters.FOCUS_MODE_CONTINUOUS_PICTURE))
                settings.setFocusMode(Camera.Parameters.FOCUS_MODE_CONTINUOUS_PICTURE);
            camera.setParameters(settings);
            int rotation=getWindowManager().getDefaultDisplay().getRotation()*90;
            int angle=info.facing==Camera.CameraInfo.CAMERA_FACING_FRONT ?
                    (360-(info.orientation+rotation)%360)%360 : (info.orientation-rotation+360)%360;
            camera.setDisplayOrientation(angle);
            int w=angle%180==0 ? best.width : best.height;
            int h=angle%180==0 ? best.height : best.width;
            float scale=Math.min((float)frame.getWidth()/w,(float)frame.getHeight()/h);
            if (scale>0) preview.setLayoutParams(new FrameLayout.LayoutParams(Math.round(w*scale),Math.round(h*scale),Gravity.CENTER));
            camera.setPreviewDisplay(preview.getHolder());
            camera.startPreview();
            camera.setOneShotPreviewCallback(this::scanFrame);
        } catch (Exception error) {
            closeCamera();
            manual("Camera unavailable. Enter the address and code to pair.");
        }
    }

    private void scanFrame(byte[] pixels,Camera source) {
        if (source!=camera || !resumed || isFinishing()) return;
        Camera.Size size=source.getParameters().getPreviewSize();
        if (pixels!=null && pixels.length>=size.width*size.height) {
            try {
                PlanarYUVLuminanceSource y=new PlanarYUVLuminanceSource(pixels,size.width,size.height,
                        0,0,size.width,size.height,false);
                String text=reader.decode(new BinaryBitmap(new HybridBinarizer(y))).getText();
                if (parsePairing(text)!=null) {
                    setResult(RESULT_OK,new Intent().putExtra("payload",text));
                    finish();
                    return;
                }
                hint.setText("Scan the pairing QR code shown by Modulez Player");
            } catch (ReaderException ignored) {
                // No complete QR code in this frame.
            } finally { reader.reset(); }
        }
        preview.postDelayed(() -> {
            if (camera==source && resumed && !isFinishing()) source.setOneShotPreviewCallback(this::scanFrame);
        },150);
    }
}
