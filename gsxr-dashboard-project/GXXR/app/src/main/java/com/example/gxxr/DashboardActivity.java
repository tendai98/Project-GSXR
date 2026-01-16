package com.example.gxxr;

import static android.view.View.GONE;
import static android.view.View.INVISIBLE;
import static android.view.View.VISIBLE;
import static android.widget.Toast.LENGTH_SHORT;

import android.content.Context;
import android.content.pm.ActivityInfo;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.os.SystemClock;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.VideoView;

import androidx.activity.EdgeToEdge;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.constraintlayout.widget.ConstraintLayout;

import org.json.JSONException;
import org.json.JSONObject;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class DashboardActivity extends AppCompatActivity implements SensorEventListener{

    private SensorManager sensorManager;
    private LinearLayout lapTimerView;
    private ConstraintLayout mainDashboardLayoutView;
    private ArcView frontBrakeTriggerIndicator, rearBrakeTriggerIndicator, tachometerBar;
    private ArcView leftLeanAngleMeter, rightLeanAngleMeter, maxLeftLeanAngleMeter, maxRightLeanAngleMeter;
    private ImageView stopWatchText, gpsSignalIndicator, headlightIndicator, lapModeIndicator, sensorFaultIndicator;
    private ParallelogramView frontSuspensionOffset, rearSuspensionOffset;
    private ParallelogramView leftTireSectionTempIndicator, centerTireSectionTempIndicator;
    private ParallelogramView frontRightBrakeDiskTempIndicator, coolantTempIndicator;
    private TextView dashboardClockTimer, lapTimer, speedCounter, gearIndicator, coolantTemperatureValue;
    private TextView leanAngleTxt;
    private TextView maxLeftLeanAngleTxt, maxRightLeanAngleTxt;
    private DatagramSocket sensorDatagramSocket;
    private byte[] messageBuf;
    private byte[] rawSensorDataBuff;
    private String systemSensorParameters;
    private final String subscribeMessage = "REQ";
    private InetAddress dataLoggerAddress;
    private final int dataLoggerPort = 9100;
    private final int MAX_FRAME_SIZE = 1880;
    private final int CALIBRATION_OFFSET_ANGLE = 5;
    private DatagramPacket messagePacket;
    private Handler dashboardInstrumentUIHandler_h1, dashboardInstrumentUIHandler_h2;
    private float[] rotationMatrix = new float[9];
    private float[] orientation = new float[3];
    private List<Integer> activeErrorCodes;
    private final double TEST_START_POINT = 0, TEST_END_POINT = 100;
    private final double MIN_BRAKE_DISK_TEMP = 0,  MAX_BRAKE_DISK_TEMP = 100;
    private final double MIN_COOLANT_TEMP = 20,  MAX_COOLANT_TEMP = 120;
    private final double MIN_SUSPENSION_LEVEL = 0,  MAX_SUSPENSION_LEVEL = 1;
    private final double MIN_TYRE_TEMP = 0,  MAX_TYRE_TEMP = 100;
    private final float TEST_INCREMENTER = 0.1f;
    private final long TEST_LOOP_DELAY = 1;
    private final long TEST_HOLD_DELAY = 20;
    private final int BOOT_UP_MODE = 0, SHUTDOWN_MODE = 1;
    private final int SYSTEM_INIT = 1;
    private final int DASH_CLOCK_UPDATE = 2;
    private static final int UPDATE_INSTRUMENTS = 3 ;
    private static final int UPDATE_LAP_TIMER = 4;
    private static final int UPDATE_ERROR_CODES = 5;
    private boolean hasEnabledDataStreamUpdates = false;
    private DatagramPacket rawSensorDataPacket;
    private double LTS, CTS, FrBD, frontSuspensionStateValue, rearSuspensionStateValue;
    private Sensor gyroScopeSensor, linearAccerlerationSensor;;
    private int leanAngle, maxLeftLeanAngle = 0, maxRightLeanAngle = 0, frontBrakes, rearBrakes;;
    private int hasGPSLocked = 0, speedKph, isHeadLightTriggerOn;
    private boolean isLapTimerArmed = false, isLapModeEnabled = false;
    private final int LAP_MODE_DELAY_COUNTER = 1000;
    private int lapModeTrackCounter = 0;
    private long lapTimerStartPoint, elapsedLapTime;
    private double coolantTemperature;
    private float frontSuspensionMapValue, rearSuspensionMapValue;
    private VideoView spashVideoView;
    private Uri bootUpAnimationUri, shutdownAnimationUri;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        this.requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON, WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN, WindowManager.LayoutParams.FLAG_FULLSCREEN);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);

        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this);
        setContentView(R.layout.activity_dashboard);
        hideSystemUI();

        spashVideoView = findViewById(R.id.splash_vid);
        mainDashboardLayoutView = findViewById(R.id.main_dashboard_view);

        leftLeanAngleMeter = findViewById(R.id.left_lean_angle_meter);
        leanAngleTxt = findViewById(R.id.lean_angle_txt);
        maxLeftLeanAngleMeter = findViewById(R.id.max_left_lean_angle_meter);
        rightLeanAngleMeter = findViewById(R.id.right_lean_angle_meter);
        maxRightLeanAngleMeter = findViewById(R.id.max_right_lean_angle_meter);
        maxLeftLeanAngleTxt = findViewById(R.id.max_left_lean_angle_txt);
        maxRightLeanAngleTxt = findViewById(R.id.max_right_lean_angle_txt);
        coolantTemperatureValue = findViewById(R.id.coolant_temp_value);

        frontSuspensionOffset = findViewById(R.id.front_suspension_state);
        rearSuspensionOffset = findViewById(R.id.rear_suspension_state);

        frontRightBrakeDiskTempIndicator = findViewById(R.id.front_brake_disk_temp);
        coolantTempIndicator = findViewById(R.id.coolant_temp);
        frontBrakeTriggerIndicator = findViewById(R.id.front_brake_trigger);
        rearBrakeTriggerIndicator = findViewById(R.id.rear_brake_trigger);
        tachometerBar = findViewById(R.id.tachometer_arc_wdgt);
        leftTireSectionTempIndicator = findViewById(R.id.left_tire_section_temp);
        centerTireSectionTempIndicator = findViewById(R.id.center_tire_section_temp);
        dashboardClockTimer = findViewById(R.id.dash_clock);
        speedCounter = findViewById(R.id.speed_counter_txt);
        gearIndicator = findViewById(R.id.gear_counter_txt);
        stopWatchText = findViewById(R.id.laptimer_enable_state);
        gpsSignalIndicator = findViewById(R.id.gps_alert_indicator);
        headlightIndicator = findViewById(R.id.headlight_indicator);
        sensorFaultIndicator = findViewById(R.id.sensor_fault_indicator);

        lapModeIndicator = findViewById(R.id.lap_mode_indicator);
        lapTimerView = findViewById(R.id.lap_timer_view);
        lapTimer = findViewById(R.id.lap_timer_clk);

        activeErrorCodes = new ArrayList<>();
        sensorManager = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        gyroScopeSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
        linearAccerlerationSensor = sensorManager.getDefaultSensor(Sensor.TYPE_LINEAR_ACCELERATION);
        sensorManager.registerListener(this, gyroScopeSensor, SensorManager.SENSOR_DELAY_FASTEST);
        sensorManager.registerListener(this, linearAccerlerationSensor, SensorManager.SENSOR_DELAY_FASTEST);

        bootUpAnimationUri = Uri.parse("android.resource://" + getPackageName() + "/" + R.raw.gsxr_boot_splash_v7);
        shutdownAnimationUri = Uri.parse("android.resource://" + getPackageName() + "/" + R.raw.gsxr_shutdown_splash_v7);

        dashboardInstrumentUIHandler_h2 = new Handler(Looper.getMainLooper()){
            @Override
            public void handleMessage(@NonNull Message msg) {
                int cmd = msg.arg1;
                if (hasEnabledDataStreamUpdates) {
                    switch (cmd) {
                        case UPDATE_INSTRUMENTS:
                            try {
                                JSONObject data = new JSONObject(msg.obj.toString());

                                try {
                                    LTS = data.getDouble("front_tyre_S1");
                                    leftTireSectionTempIndicator.setScaleAmount((float) mapWidgetValue(LTS, MIN_TYRE_TEMP, MAX_TYRE_TEMP, 0, 1.43));
                                    if (activeErrorCodes.contains(R.mipmap.warning_e1)) {
                                        activeErrorCodes.remove(R.mipmap.warning_e1);
                                    }
                                } catch (Exception ignored) {
                                    if (!activeErrorCodes.contains(R.mipmap.warning_e1)) {
                                        activeErrorCodes.add(R.mipmap.warning_e1);
                                    }
                                    leftTireSectionTempIndicator.setScaleAmount(0f);
                                }

                                try {
                                    CTS = data.getDouble("front_tyre_S2");
                                    centerTireSectionTempIndicator.setScaleAmount((float) mapWidgetValue(CTS, MIN_TYRE_TEMP, MAX_TYRE_TEMP, 0, 1.43));
                                    if (activeErrorCodes.contains(R.mipmap.warning_e2)) {
                                        activeErrorCodes.remove(R.mipmap.warning_e2);
                                    }
                                } catch (Exception ignored) {
                                    if (!activeErrorCodes.contains(R.mipmap.warning_e2)) {
                                        activeErrorCodes.add(R.mipmap.warning_e2);
                                    }
                                    centerTireSectionTempIndicator.setScaleAmount(0f);
                                }

                                try {
                                    FrBD = data.getDouble("front_brakes_tps_S1");
                                    frontRightBrakeDiskTempIndicator.setScaleAmount((float) mapWidgetValue(FrBD, MIN_BRAKE_DISK_TEMP, MAX_BRAKE_DISK_TEMP, 0, 1.1));
                                    if (activeErrorCodes.contains(R.mipmap.warning_e3)) {
                                        activeErrorCodes.remove(R.mipmap.warning_e3);
                                    }
                                } catch (Exception ignored) {
                                    if (!activeErrorCodes.contains(R.mipmap.warning_e3)) {
                                        activeErrorCodes.add(R.mipmap.warning_e3);
                                    }
                                    frontRightBrakeDiskTempIndicator.setScaleAmount(0f);
                                }

                                // Disabled this code because I broke the rear brake disk sensor
                                /*try {
                                    RBD = data.getDouble("rear_brake_gps_S1");
                                    rearBrakeDiskTempIndicator.setScaleAmount((float) mapWidgetValue(RBD, MIN_BRAKE_DISK_TEMP, MAX_BRAKE_DISK_TEMP, 0, 1.1));
                                }catch (Exception ignored){
                                    rearBrakeDiskTempIndicator.setScaleAmount(0f);
                                }*/

                                try {
                                    frontBrakes = data.getInt("brakes_imu_in_d5");
                                    frontBrakeTriggerIndicator.setVisibility((frontBrakes == 1 ? INVISIBLE : VISIBLE));

                                    rearBrakes = data.getInt("brakes_imu_in_d6");
                                    rearBrakeTriggerIndicator.setVisibility((rearBrakes == 1 ? INVISIBLE : VISIBLE));

                                    // lap-mode logic to activate laptimer
                                    if (frontBrakes == 0 && rearBrakes == 0 && !isLapModeEnabled) {
                                        if (lapModeTrackCounter == LAP_MODE_DELAY_COUNTER) {
                                            lapTimerView.setVisibility(VISIBLE);
                                            lapModeIndicator.setVisibility(VISIBLE);
                                            isLapModeEnabled = true;
                                        } else {
                                            lapModeTrackCounter++;
                                        }
                                    }

                                    // Shutdown dashboard and go into standby mode
                                    if (rearBrakes == 1 && hasEnabledDataStreamUpdates) {
                                        handleDashboardBootAnimations(SHUTDOWN_MODE);
                                    }

                                    // lap-mode logic to disable timer
                                    if (frontBrakes == 0 && rearBrakes == 0 && isLapModeEnabled) {
                                        if (lapModeTrackCounter == 0) {

                                            isLapTimerArmed = false;
                                            dashboardClockTimer.setVisibility(VISIBLE);
                                            lapTimer.setTextColor(getColor(R.color.default_theme_color));
                                            lapTimer.setShadowLayer(0f, 0f, 0f, getColor(R.color.default_theme_color));
                                            stopWatchText.setImageResource(R.mipmap.gsxr_laptimer);
                                            lapTimerStartPoint = 0;

                                            lapTimerView.setVisibility(INVISIBLE);
                                            lapModeIndicator.setVisibility(INVISIBLE);
                                            isLapModeEnabled = false;
                                        } else {
                                            lapModeTrackCounter--;
                                        }
                                    }

                                    if (activeErrorCodes.contains(R.mipmap.warning_e4)) {
                                        activeErrorCodes.remove(R.mipmap.warning_e4);
                                    }
                                } catch (Exception ignored) {
                                    if (!activeErrorCodes.contains(R.mipmap.warning_e4)) {
                                        activeErrorCodes.add(R.mipmap.warning_e4);
                                    }
                                }

                                try {
                                    frontSuspensionStateValue = data.getDouble("front_imu_sonar_mm");
                                    frontSuspensionMapValue = 1 - ((float) mapWidgetValue(frontSuspensionStateValue, 16, 80, MIN_SUSPENSION_LEVEL, MAX_SUSPENSION_LEVEL));
                                    if (frontSuspensionMapValue >= 0) {
                                        frontSuspensionOffset.setScaleAmount(frontSuspensionMapValue);
                                    }

                                    if (activeErrorCodes.contains(R.mipmap.warning_e5)) {
                                        activeErrorCodes.remove(R.mipmap.warning_e5);
                                    }
                                } catch (Exception ignored) {
                                    if (!activeErrorCodes.contains(R.mipmap.warning_e5)) {
                                        activeErrorCodes.add(R.mipmap.warning_e5);
                                    }
                                    frontSuspensionOffset.setScaleAmount(0f);
                                }

                                try {
                                    rearSuspensionStateValue = data.getDouble("rear_brake_gps_rear_ss_mm");
                                    rearSuspensionMapValue = 1 - ((float) mapWidgetValue(rearSuspensionStateValue, 32, 128, MIN_SUSPENSION_LEVEL, MAX_SUSPENSION_LEVEL));
                                    if (rearSuspensionMapValue >= 0) {
                                        rearSuspensionOffset.setScaleAmount(rearSuspensionMapValue);
                                    }

                                    if (activeErrorCodes.contains(R.mipmap.warning_e6)) {
                                        activeErrorCodes.remove(R.mipmap.warning_e6);
                                    }
                                } catch (Exception ignored) {
                                    if (!activeErrorCodes.contains(R.mipmap.warning_e6)) {
                                        activeErrorCodes.add(R.mipmap.warning_e6);
                                    }
                                    rearSuspensionOffset.setScaleAmount(0f);
                                }

                                try {
                                    hasGPSLocked = data.getInt("rear_brake_gps_gps_fix");
                                    if (hasGPSLocked == 1) {
                                        gpsSignalIndicator.setVisibility(VISIBLE);
                                    } else {
                                        gpsSignalIndicator.setVisibility(INVISIBLE);
                                    }

                                    if (activeErrorCodes.contains(R.mipmap.warning_e7)) {
                                        activeErrorCodes.remove(R.mipmap.warning_e7);
                                    }
                                } catch (Exception ignored) {
                                    if (!activeErrorCodes.contains(R.mipmap.warning_e7)) {
                                        activeErrorCodes.add(R.mipmap.warning_e7);
                                    }
                                }

                                try {
                                    speedKph = (int) data.getDouble("rear_brake_gps_wheel_kmh");
                                    if (speedKph >= 0 && speedKph < 300) {
                                        speedCounter.setText(String.format(Locale.ENGLISH, "%03d", speedKph));
                                    }

                                    if (activeErrorCodes.contains(R.mipmap.warning_e8)) {
                                        activeErrorCodes.remove(R.mipmap.warning_e8);
                                    }
                                } catch (Exception ignored) {
                                    if (!activeErrorCodes.contains(R.mipmap.warning_e8)) {
                                        activeErrorCodes.add(R.mipmap.warning_e8);
                                    }
                                }

                                try {
                                    coolantTemperature = data.getDouble("port4_d6_int_tempC");
                                    if (coolantTemperature > MIN_COOLANT_TEMP) {
                                        coolantTempIndicator.setScaleAmount((float) mapWidgetValue(coolantTemperature, MIN_COOLANT_TEMP, MAX_COOLANT_TEMP, 0, 1.1));
                                        coolantTemperatureValue.setText(String.format(Locale.ENGLISH, "%03d", (int) coolantTemperature));
                                    }

                                    if (activeErrorCodes.contains(R.mipmap.warning_e9)) {
                                        activeErrorCodes.remove(R.mipmap.warning_e9);
                                    }
                                } catch (Exception ignored) {
                                    if (!activeErrorCodes.contains(R.mipmap.warning_e9)) {
                                        activeErrorCodes.add(R.mipmap.warning_e9);
                                    }
                                    coolantTempIndicator.setScaleAmount(0f);
                                }

                                try {
                                    isHeadLightTriggerOn = data.getInt("port4_d6_int_d6");

                                    if (isLapModeEnabled) {
                                        if (isHeadLightTriggerOn == 1 && rearBrakes == 1 && frontBrakes == 0 && !isLapTimerArmed) {
                                            isLapTimerArmed = true;
                                            lapTimerStartPoint = SystemClock.elapsedRealtime();
                                            dashboardClockTimer.setVisibility(INVISIBLE);
                                            lapTimer.setTextColor(0xff000000);
                                            lapTimer.setShadowLayer(0f, 0f, 0f, 0x00000000);
                                            stopWatchText.setImageResource(R.mipmap.gsxr_laptimer_on);

                                        } else if (isHeadLightTriggerOn == 1 && rearBrakes == 0 && frontBrakes == 1 && isLapTimerArmed) {
                                            isLapTimerArmed = false;
                                            dashboardClockTimer.setVisibility(VISIBLE);
                                            lapTimer.setTextColor(getColor(R.color.default_theme_color));
                                            lapTimer.setShadowLayer(0f, 0f, 0f, getColor(R.color.default_theme_color));
                                            stopWatchText.setImageResource(R.mipmap.gsxr_laptimer);
                                            lapTimerStartPoint = 0;
                                        }
                                    }

                                    if (isHeadLightTriggerOn == 1) {
                                        headlightIndicator.setVisibility(VISIBLE);
                                    } else {
                                        headlightIndicator.setVisibility(INVISIBLE);
                                    }

                                    if (activeErrorCodes.contains(R.mipmap.warning_e10)) {
                                        activeErrorCodes.remove(R.mipmap.warning_e10);
                                    }
                                } catch (Exception ignored) {
                                    ;
                                    if (!activeErrorCodes.contains(R.mipmap.warning_e10)) {
                                        activeErrorCodes.add(R.mipmap.warning_e10);
                                    }
                                }

                            } catch (JSONException ignored) {
                                //ignored.printStackTrace();
                            }
                            break;

                        case UPDATE_LAP_TIMER:
                            try {
                                long millis = (long) msg.obj;
                                long hours = millis / 3600000;
                                long remainder = millis % 3600000;

                                long minutes = remainder / 60000;
                                remainder = remainder % 60000;

                                long seconds = remainder / 1000;
                                long ms = remainder % 1000;
                                long centiseconds = ms / 10;

                                String formattedTime = String.format(
                                        Locale.getDefault(),
                                        "%02d:%02d:%02d:%02d",
                                        hours, minutes, seconds, centiseconds
                                );

                                lapTimer.setText(formattedTime);

                            } catch (Exception e) {
                                e.printStackTrace();
                            }

                            break;

                        case UPDATE_ERROR_CODES:
                            try {
                                int errorCodeId = (int) msg.obj;
                                if (errorCodeId != -1) {
                                    sensorFaultIndicator.setVisibility(VISIBLE);
                                    sensorFaultIndicator.setImageResource(errorCodeId);
                                } else {
                                    sensorFaultIndicator.setVisibility(INVISIBLE);
                                    sensorFaultIndicator.setImageResource(R.mipmap.warning);
                                }
                            } catch (Exception e) {
                                e.printStackTrace();
                            }

                            break;


                        default:
                            break;
                    }
                }else{
                    // Control boot and shutdown sequence
                    if (cmd == UPDATE_INSTRUMENTS) {
                        try {
                            JSONObject data = new JSONObject(msg.obj.toString());
                            int isSystemOnline = data.getInt("brakes_imu_in_d6");
                            if (isSystemOnline == 0) {
                                hasEnabledDataStreamUpdates = true;
                                handleDashboardBootAnimations(BOOT_UP_MODE);
                            }

                        } catch (Exception ignored) {}
                    }

                }
            }
        };

        dashboardInstrumentUIHandler_h1 = new Handler(Looper.getMainLooper()){
            @Override
            public void handleMessage(@NonNull Message msg) {
                int cmd = msg.arg1;

                switch (cmd){
                    case BOOT_UP_MODE:
                        double value = (double) msg.obj;
                        double tachometerMapValue = mapWidgetValue(value, TEST_START_POINT, TEST_END_POINT, 0.0f, 217f);
                        int speedValue = (int) mapWidgetValue(value, TEST_START_POINT, TEST_END_POINT, 0.0f, 299.0f);

                        gearIndicator.setText("-");
                        gearIndicator.setShadowLayer(10.0f, 0f, 0f, getColor(R.color.default_theme_color));
                        gearIndicator.setTextColor(getColor(R.color.default_theme_color));

                        leftTireSectionTempIndicator.setScaleAmount((float) mapWidgetValue(value, TEST_START_POINT, TEST_END_POINT, 0.0f, 1.43f));
                        centerTireSectionTempIndicator.setScaleAmount((float) mapWidgetValue(value, TEST_START_POINT, TEST_END_POINT, 0.0f, 1.43f));
                        frontRightBrakeDiskTempIndicator.setScaleAmount((float) mapWidgetValue(value, TEST_START_POINT, TEST_END_POINT, 0.0f, 1.1f));
                        coolantTempIndicator.setScaleAmount((float) mapWidgetValue(value, TEST_START_POINT, TEST_END_POINT, 0.0f, 1.1f));
                        rearSuspensionOffset.setScaleAmount((float) mapWidgetValue(value, TEST_START_POINT, TEST_END_POINT, MIN_SUSPENSION_LEVEL, MAX_SUSPENSION_LEVEL));
                        frontSuspensionOffset.setScaleAmount((float) mapWidgetValue(value, TEST_START_POINT, TEST_END_POINT, MIN_SUSPENSION_LEVEL, MAX_SUSPENSION_LEVEL));

                        tachometerBar.setArcSweepAngle((float) tachometerMapValue);
                        speedCounter.setText(String.format(Locale.ENGLISH, "%03d", speedValue));

                        if(tachometerMapValue < 45) {
                            tachometerBar.setArcColor(getColor(R.color.lower_power_band));
                            tachometerBar.setArcShadowColor(getColor(R.color.lower_power_band));
                        }

                        if(tachometerMapValue > 45 && value < 183){
                            tachometerBar.setArcColor(getColor(R.color.default_theme_color));
                            tachometerBar.setArcShadowColor(getColor(R.color.default_theme_color));
                        }if(tachometerMapValue > 182){
                            if(((int) tachometerMapValue % 2) == 1) {
                                tachometerBar.setArcColor(getColor(R.color.blink_white));
                                tachometerBar.setArcShadowColor(getColor(R.color.blink_white));
                            }else{
                                tachometerBar.setArcColor(getColor(R.color.rev_limiter_zone));
                                tachometerBar.setArcShadowColor(getColor(R.color.rev_limiter_zone));
                            }
                        }

                        tachometerBar.setArcShadowOffset(0f,0f);
                        tachometerBar.setArcShadowRadius(3f);
                        break;

                    case SYSTEM_INIT:
                        initializeDashboardInstruments();
                        break;

                    case DASH_CLOCK_UPDATE:
                        String clockData = msg.obj.toString();
                        dashboardClockTimer.setText(clockData);

                    default:
                        break;
                }

            }
        };

        runSensorDataStreamReceiverThread();
        runLapTimerClockThread();
        runErrorCodeHandlerThread();
    }

    private void  handleDashboardBootAnimations(int mode) {

        switch (mode) {

            case BOOT_UP_MODE:
                spashVideoView.setVideoURI(bootUpAnimationUri);
                spashVideoView.setOnCompletionListener(mp -> {
                    mp.stop();
                    spashVideoView.setVisibility(GONE);
                    mainDashboardLayoutView.setVisibility(VISIBLE);
                    runDashboardTestThread();
                });

                spashVideoView.start();
                break;

            case SHUTDOWN_MODE:
                hasEnabledDataStreamUpdates = false;
                spashVideoView.setVisibility(VISIBLE);
                mainDashboardLayoutView.setVisibility(INVISIBLE);
                spashVideoView.setVideoURI(shutdownAnimationUri);
                spashVideoView.setOnCompletionListener(mp -> {
                    mp.stop();
                });

                spashVideoView.start();
                break;

        }

    }

    private void runSensorDataStreamReceiverThread(){
        Thread sensorDataStreamReceiver = new Thread(new Runnable() {
            @Override
            public void run() {
                try{
                    Thread.sleep(5000);
                    sensorDatagramSocket = new DatagramSocket();
                    messageBuf = subscribeMessage.getBytes();
                    dataLoggerAddress = InetAddress.getLocalHost();
                    messagePacket = new DatagramPacket(messageBuf, messageBuf.length, dataLoggerAddress, dataLoggerPort);
                    rawSensorDataBuff = new byte[MAX_FRAME_SIZE];
                    rawSensorDataPacket = new DatagramPacket(rawSensorDataBuff, MAX_FRAME_SIZE, dataLoggerAddress, dataLoggerPort);

                    while (true){
                        Message sensorDataMessage = new Message();
                        sensorDatagramSocket.send(messagePacket);
                        sensorDatagramSocket.receive(rawSensorDataPacket);
                        systemSensorParameters = new String(rawSensorDataPacket.getData(), rawSensorDataPacket.getOffset(), rawSensorDataPacket.getLength(), StandardCharsets.UTF_8);
                        sensorDataMessage.arg1 = UPDATE_INSTRUMENTS;
                        sensorDataMessage.obj = systemSensorParameters;
                        dashboardInstrumentUIHandler_h2.sendMessage(sensorDataMessage);
                        Thread.sleep(1);
                    }
                }catch (Exception e){
                    e.printStackTrace();
                }
            }
        });

        sensorDataStreamReceiver.start();
    }

    private void runLapTimerClockThread(){
        Thread lapTimerThread = new Thread(new Runnable() {
            @Override
            public void run() {
                while (true){
                    if(isLapTimerArmed){
                        try{
                            elapsedLapTime = SystemClock.elapsedRealtime() - lapTimerStartPoint;
                            Message timerMessage = new Message();
                            timerMessage.arg1 = UPDATE_LAP_TIMER;
                            timerMessage.obj = elapsedLapTime;
                            dashboardInstrumentUIHandler_h2.sendMessage(timerMessage);
                            Thread.sleep(1);
                        }catch (Exception e){
                            e.printStackTrace();
                        }
                    }
                }
            }
        });

        lapTimerThread.start();
    }

    private void runErrorCodeHandlerThread(){
        Thread errorCodeThread = new Thread(new Runnable() {
            @Override
            public void run() {
                while (true){
                    try{
                        if(hasEnabledDataStreamUpdates) {
                            if (!activeErrorCodes.isEmpty()) {

                                for (int errorCode : activeErrorCodes) {
                                    Message errorCodeMessage = new Message();
                                    errorCodeMessage.arg1 = UPDATE_ERROR_CODES;
                                    errorCodeMessage.obj = errorCode;
                                    dashboardInstrumentUIHandler_h2.sendMessage(errorCodeMessage);
                                    Thread.sleep(500);
                                }

                                activeErrorCodes.clear();

                            } else {
                                Message errorCodeMessage = new Message();
                                errorCodeMessage.arg1 = UPDATE_ERROR_CODES;
                                errorCodeMessage.obj = -1;
                                dashboardInstrumentUIHandler_h2.sendMessage(errorCodeMessage);
                                Thread.sleep(500);
                            }
                        }
                    }catch (Exception e){
                        e.printStackTrace();
                    }
                }
            }
        });

        errorCodeThread.start();
    }

    private void runDashboardTestThread(){

        Thread counterThread = new Thread(new Runnable() {

            @Override
            public void run() {
                try {
                    Thread.sleep(500);

                    /*
                    // Main Test data Loop on loading instruments
                    for(int loop = 0; loop < 1; loop++) {

                        // Forward Test subloop
                        for (double value = TEST_START_POINT; value <= TEST_END_POINT; value += TEST_INCREMENTER) {
                            Message counterObject = new Message();

                            counterObject.arg1 = BOOT_UP_MODE;
                            counterObject.obj = value;
                            dashboardInstrumentUIHandler_h1.sendMessage(counterObject);
                            Thread.sleep(TEST_LOOP_DELAY);
                        }

                        Thread.sleep(TEST_HOLD_DELAY);

                        // Reverse Test subloop
                        for (double value = TEST_END_POINT; value >= TEST_START_POINT; value -= TEST_INCREMENTER) {
                            Message counterObject = new Message();

                            counterObject.arg1 = BOOT_UP_MODE;
                            counterObject.obj = value;
                            dashboardInstrumentUIHandler_h1.sendMessage(counterObject);
                            Thread.sleep(TEST_LOOP_DELAY);
                        }
                    }*/

                    Message counterObject = new Message();
                    counterObject.arg1 = SYSTEM_INIT;
                    counterObject.obj = 0f;
                    dashboardInstrumentUIHandler_h1.sendMessage(counterObject);
                }catch (Exception e){
                    e.printStackTrace();
                }
            }
        });

        counterThread.start();
    }

    private void runDashClock(){
        Thread dashClockThread = new Thread(new Runnable() {
            @Override
            public void run() {
                while (true){
                    try{
                        Message dashClockMessage = new Message();
                        String currentTime = new SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(new Date());
                        dashClockMessage.arg1 = DASH_CLOCK_UPDATE;
                        dashClockMessage.obj = currentTime;
                        dashboardInstrumentUIHandler_h1.handleMessage(dashClockMessage);
                        Thread.sleep(999);
                    }catch (Exception e){
                        e.printStackTrace();
                    }
                }
            }
        });

        dashClockThread.start();
    }

    private void initializeDashboardInstruments() {

        lapTimer.setText(R.string.zero_timer_state);
        lapTimer.setTextColor(getColor(R.color.default_theme_color));
        lapTimer.setShadowLayer(10f, 0f, 0f, getColor(R.color.default_theme_color));
        stopWatchText.setImageResource(R.mipmap.gsxr_laptimer);

        leftLeanAngleMeter.setArcSweepAngle(-2f);
        rightLeanAngleMeter.setArcSweepAngle(2f);
        leanAngleTxt.setText(String.format(Locale.ENGLISH,"°%02.0f", 0f));
        maxLeftLeanAngleTxt.setText(String.format(Locale.ENGLISH,"°%02.0f", 0f));
        maxRightLeanAngleTxt.setText(String.format(Locale.ENGLISH,"%02.0f°", 0f));

        gearIndicator.setText("-");
        gearIndicator.setShadowLayer(10.0f, 0f, 0f, getColor(R.color.neutral_gear_color));
        gearIndicator.setTextColor(getColor(R.color.neutral_gear_color));

        frontBrakeTriggerIndicator.setVisibility(INVISIBLE);
        rearBrakeTriggerIndicator.setVisibility(INVISIBLE);
        frontBrakeTriggerIndicator.setVisibility(INVISIBLE);
        rearBrakeTriggerIndicator.setVisibility(INVISIBLE);
        headlightIndicator.setVisibility(INVISIBLE);
        lapModeIndicator.setVisibility(INVISIBLE);
        sensorFaultIndicator.setVisibility(INVISIBLE);
        coolantTemperatureValue.setText(R.string.default_temp);

        maxRightLeanAngleMeter.setArcSweepAngle(0);
        maxLeftLeanAngleMeter.setArcSweepAngle(0);
        gpsSignalIndicator.setVisibility(INVISIBLE);
        lapTimerView.setVisibility(INVISIBLE);

        hasEnabledDataStreamUpdates = true;

        runDashClock();
    }
    private double mapWidgetValue(double a, double inputMin, double inputMax, double outputMin, double outputMax){
        return (a - inputMin) * (outputMax - outputMin) / (inputMax - inputMin) - outputMin;
    }

    private void hideSystemUI() {
        View decorView = getWindow().getDecorView();
        decorView.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        );
    }



    @Override
    public void onSensorChanged(SensorEvent event) {
        int typeOfSensor = event.sensor.getType();

        if (hasEnabledDataStreamUpdates) {

            if (typeOfSensor == Sensor.TYPE_ROTATION_VECTOR) {
                SensorManager.getRotationMatrixFromVector(rotationMatrix, event.values);
                SensorManager.getOrientation(rotationMatrix, orientation);

                leanAngle = (int) Math.toDegrees(orientation[1]) - CALIBRATION_OFFSET_ANGLE; // Compensate for dashboard mounting

                if (leanAngle >= 0) {
                    leftLeanAngleMeter.setArcSweepAngle(-((float) leanAngle));
                    leanAngleTxt.setText(String.format(Locale.ENGLISH, "°%02d", leanAngle));

                    if (leanAngle > maxLeftLeanAngle) {
                        maxLeftLeanAngle = leanAngle;
                        maxLeftLeanAngleTxt.setText(String.format(Locale.ENGLISH, "°%02d", maxLeftLeanAngle));
                        maxLeftLeanAngleMeter.setArcSweepAngle(-((float) leanAngle));
                    }
                }

                if (leanAngle <= 0) {
                    rightLeanAngleMeter.setArcSweepAngle(-((float) leanAngle));
                    leanAngleTxt.setText(String.format(Locale.ENGLISH, "%02d°", -leanAngle));

                    if (-leanAngle > maxRightLeanAngle) {
                        maxRightLeanAngle = -leanAngle; //Negate the value so that its positive
                        maxRightLeanAngleTxt.setText(String.format(Locale.ENGLISH, "%02d°", maxRightLeanAngle));
                        maxRightLeanAngleMeter.setArcSweepAngle(-((float) leanAngle));
                    }
                }


            }
        }

    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {

    }

    @Override
    protected void onResume() {
        super.onResume();
        if(gyroScopeSensor != null){
            sensorManager.registerListener(this, gyroScopeSensor, SensorManager.SENSOR_DELAY_FASTEST);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        sensorManager.unregisterListener(this);
    }
}