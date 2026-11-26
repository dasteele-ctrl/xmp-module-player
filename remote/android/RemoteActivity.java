package org.modulez.remote.preview;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.widget.*;
import org.json.*;
import java.io.*;
import java.net.*;
import java.security.MessageDigest;
import java.security.cert.*;
import java.util.*;
import java.util.concurrent.*;
import javax.net.ssl.*;

public class RemoteActivity extends Activity {
    private final Handler ui = new Handler();
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private LinearLayout root;
    private EditText address, code;
    private TextView status, title, format, volumeText;
    private Button pair, toggle;
    private SeekBar volume;
    private ArrayAdapter<String> tracks;
    private SharedPreferences prefs;
    private Session session;
    private long revision = -1;
    private boolean foreground, busy, dragging;
    private final Runnable poll = () -> refresh(null, -1);

    private static class Session {
        final String host, pin, token;
        Session(String host, String pin, String token) {
            this.host = host; this.pin = pin; this.token = token;
        }
    }

    private static class HttpError extends IOException {
        final int status;
        HttpError(int status) { super("Dell returned " + status); this.status = status; }
    }

    private static String digest(X509Certificate certificate) throws CertificateException {
        try {
            StringBuilder out = new StringBuilder();
            for (byte b : MessageDigest.getInstance("SHA-256").digest(certificate.getEncoded()))
                out.append(String.format(Locale.US, "%02x", b & 255));
            return out.toString();
        } catch (Exception ex) { throw new CertificateException(ex); }
    }

    private static JSONObject call(Session s, String path, String method, String secret,
                                   int value, long revision) throws Exception {
        final String[] fullPin = new String[1];
        X509TrustManager trust = new X509TrustManager() {
            public void checkClientTrusted(X509Certificate[] chain, String auth) throws CertificateException {
                throw new CertificateException("Client certificate unsupported");
            }
            public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
            public void checkServerTrusted(X509Certificate[] chain, String auth) throws CertificateException {
                if (chain == null || chain.length == 0) throw new CertificateException("Missing certificate");
                String fingerprint = digest(chain[0]);
                if (!fingerprint.startsWith(s.pin)) throw new CertificateException("Pairing fingerprint changed");
                fullPin[0] = fingerprint;
            }
        };
        SSLContext tls = SSLContext.getInstance("TLS");
        tls.init(null, new TrustManager[]{trust}, null);
        HttpsURLConnection c = (HttpsURLConnection)new URL("https", s.host, 48788, path)
                .openConnection(Proxy.NO_PROXY);
        try {
            c.setSSLSocketFactory(tls.getSocketFactory());
            // The code on the Dell authenticates its self-signed certificate.
            c.setHostnameVerifier((host, ssl) -> host.equals(s.host));
            c.setInstanceFollowRedirects(false);
            c.setConnectTimeout(3500); c.setReadTimeout(5000);
            c.setUseCaches(false); c.setRequestMethod(method);
            if (secret != null) c.setRequestProperty("X-Pair-Code", secret);
            if (!s.token.isEmpty()) c.setRequestProperty("X-Remote-Token", s.token);
            if (value >= 0) c.setRequestProperty("X-Value", Integer.toString(value));
            if (revision >= 0) c.setRequestProperty("X-Revision", Long.toString(revision));
            int result = c.getResponseCode();
            if (result < 200 || result >= 300) throw new HttpError(result);
            if (result == 204) return new JSONObject();
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (InputStream input = c.getInputStream()) {
                byte[] buffer = new byte[8192]; int n;
                while ((n = input.read(buffer)) != -1) bytes.write(buffer, 0, n);
            }
            JSONObject obj = new JSONObject(bytes.toString("UTF-8"));
            if (secret != null) obj.put("pin", fullPin[0]);
            return obj;
        } finally { c.disconnect(); }
    }

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        prefs = getSharedPreferences("pair", MODE_PRIVATE);
        if (prefs.contains("token")) session = new Session(prefs.getString("host",""),
                prefs.getString("pin",""), prefs.getString("token",""));
        render();
    }

    private int dp(int n) { return (int)(n * getResources().getDisplayMetrics().density + .5f); }
    private TextView label(LinearLayout box, String text, int size) {
        TextView v = new TextView(this); v.setTextColor(Color.rgb(228,235,235));
        v.setTextSize(size); v.setText(text); box.addView(v); return v;
    }
    private Button button(LinearLayout box, String text, Runnable action) {
        Button b = new Button(this); b.setText(text); b.setAllCaps(false);
        b.setOnClickListener(v -> action.run()); box.addView(b); return b;
    }
    private EditText input(String hint) {
        EditText field = new EditText(this); field.setSingleLine(true); field.setHint(hint);
        field.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        root.addView(field); return field;
    }
    private void render() {
        root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(17,21,23));
        root.setPadding(dp(16),dp(12),dp(16),dp(8));
        root.setOnApplyWindowInsetsListener((v,i) -> {
            v.setPadding(dp(16)+i.getSystemWindowInsetLeft(), dp(12)+i.getSystemWindowInsetTop(),
                    dp(16)+i.getSystemWindowInsetRight(), dp(8)+i.getSystemWindowInsetBottom());
            return i;
        });
        setContentView(root);
        if (session == null) {
            label(root,"Modulez Remote",28);
            label(root,"Open Pair remote on your Dell, then enter the address and code shown there.",16);
            address = input("Dell address"); address.setText(prefs.getString("host",""));
            code = input("Pairing code");
            pair = button(root,"Pair with Dell",this::pair);
            status = label(root,"",15);
        } else {
            label(root,"Now Playing",16); title = label(root,"Connecting…",24);
            title.setMaxLines(2); format = label(root,"",16); format.setMaxLines(2);
            LinearLayout row = new LinearLayout(this); root.addView(row);
            button(row,"Previous",() -> refresh("/previous",-1));
            toggle = button(row,"Play",() -> refresh("/toggle",-1));
            button(row,"Next",() -> refresh("/next",-1));
            volumeText = label(root,"Volume",16); volume = new SeekBar(this); root.addView(volume);
            volume.setMax(100);
            volume.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onStartTrackingTouch(SeekBar bar) { dragging=true; }
                public void onStopTrackingTouch(SeekBar bar) { dragging=false; refresh("/volume",bar.getProgress()); }
                public void onProgressChanged(SeekBar bar,int n,boolean user) { volumeText.setText("Dell volume  " + n + "%"); }
            });
            LinearLayout browse = new LinearLayout(this);
            browse.setGravity(android.view.Gravity.CENTER_VERTICAL);
            root.addView(browse);
            label(browse,"Playlist",20).setLayoutParams(new LinearLayout.LayoutParams(0,-2,1));
            button(browse,"Radio stations",this::chooseStation);
            ListView list = new ListView(this);
            root.addView(list,new LinearLayout.LayoutParams(-1,0,1));
            tracks = new ArrayAdapter<>(this,android.R.layout.simple_list_item_1,new ArrayList<>());
            list.setAdapter(tracks); list.setOnItemClickListener((a,v,i,id) -> refresh("/select",i));
            status = label(root,"Connecting to " + session.host,14);
            button(root,"Forget Dell",this::forget);
        }
    }
    private void chooseStation() {
        final Session target=session;
        worker.execute(() -> {
            try {
                JSONArray list=call(target,"/stations","GET",null,-1,-1).getJSONArray("stations");
                String[] names=new String[list.length()], ids=new String[list.length()];
                for (int i=0;i<names.length;i++) {
                    JSONObject station=list.getJSONObject(i);
                    names[i]=station.getString("group")+"  ·  "+station.getString("name");
                    ids[i]=station.getString("id");
                }
                ui.post(() -> {
                    if (!foreground || session!=target || isDestroyed()) return;
                    new android.app.AlertDialog.Builder(this).setTitle("Radio stations")
                        .setItems(names,(dialog,index) -> refresh("/station/"+ids[index],-1))
                        .setNegativeButton("Close",null).show();
                });
            } catch (Exception ex) { ui.post(() -> {
                if (foreground && session==target && !isDestroyed())
                    Toast.makeText(this,"Couldn’t load stations. "+message(ex),Toast.LENGTH_LONG).show();
            }); }
        });
    }
    private void forget() {
        ui.removeCallbacks(poll); session=null; revision=-1; busy=false;
        prefs.edit().remove("pin").remove("token").apply(); render();
    }
    private void pair() {
        String host = address.getText().toString().trim();
        String entered = code.getText().toString().toLowerCase(Locale.US).replaceAll("[-\\s]","");
        if (!android.util.Patterns.IP_ADDRESS.matcher(host).matches() || host.contains(":") ||
                !entered.matches("[0-9a-f]{14}")) {
            status.setText("Enter the Dell’s IPv4 address and the complete pairing code."); return;
        }
        Session candidate = new Session(host, entered.substring(0,8), "");
        pair.setEnabled(false); status.setText("Pairing…");
        worker.execute(() -> {
            try {
                JSONObject reply = call(candidate,"/pair","POST",entered.substring(8),-1,-1);
                Session paired = new Session(host,reply.getString("pin"),reply.getString("token"));
                ui.post(() -> {
                    if (isDestroyed()) return;
                    session=paired;
                    prefs.edit().putString("host",host).putString("pin",paired.pin).putString("token",paired.token).apply();
                    render(); refresh(null,-1);
                });
            } catch (Exception ex) { ui.post(() -> {
                if (!isDestroyed()) { pair.setEnabled(true); status.setText("Pairing failed. Check Wi-Fi, address and code. " + message(ex)); }
            }); }
        });
    }
    private String message(Exception ex) {
        if (ex instanceof SSLException) return "Check the pairing code. Pair again if the Dell restarted.";
        if (ex instanceof HttpError && ((HttpError)ex).status==410) return "Start a new pairing session on the Dell.";
        return ex.getLocalizedMessage();
    }
    private void refresh(String action, int value) {
        if (session == null || !foreground || (action == null && busy)) return;
        ui.removeCallbacks(poll);
        final Session target=session; final long seen=revision; busy=true;
        worker.execute(() -> {
            try {
                if (action != null) call(target,action,"POST",null,value,seen);
                JSONObject state=call(target,"/state","GET",null,-1,-1);
                JSONObject queue=state.getLong("revision")==seen ? null : call(target,"/playlist","GET",null,-1,-1);
                ui.post(() -> {
                    if (session!=target || isDestroyed()) return;
                    title.setText(state.optString("title")); format.setText(state.optString("format"));
                    toggle.setText(state.optBoolean("playing") ? (state.optBoolean("radio") ? "Stop" : "Pause") : "Play");
                    if (!dragging) { volume.setProgress(state.optInt("volume")); volumeText.setText("Dell volume  " + state.optInt("volume") + "%"); }
                    if (queue != null) {
                        revision=queue.optLong("revision"); JSONArray rows=queue.optJSONArray("tracks");
                        tracks.setNotifyOnChange(false); tracks.clear();
                        for (int i=0;i<rows.length();i++) tracks.add(rows.optString(i));
                        tracks.notifyDataSetChanged();
                    }
                    status.setText("Connected to " + target.host); schedule();
                });
            } catch (Exception ex) { ui.post(() -> {
                if (session!=target || isDestroyed()) return;
                status.setText("Connection issue. " + message(ex)); schedule();
            }); }
        });
    }
    private void schedule() {
        busy=false; ui.removeCallbacks(poll); if (foreground) ui.postDelayed(poll,1500);
    }
    @Override protected void onResume() { super.onResume(); foreground=true; refresh(null,-1); }
    @Override protected void onPause() { foreground=false; ui.removeCallbacks(poll); super.onPause(); }
    @Override protected void onDestroy() { ui.removeCallbacks(poll); worker.shutdownNow(); super.onDestroy(); }
}
