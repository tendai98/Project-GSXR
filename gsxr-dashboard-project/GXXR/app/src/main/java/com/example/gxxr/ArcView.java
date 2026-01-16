package com.example.gxxr; // <- your package

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

public class ArcView extends View {

    private Paint paint;
    private RectF arcRect;

    // Arc config
    private float startAngle = 180f;
    private float sweepAngle = 180f;
    private float strokeWidth = 20f;

    public final float MIN_ARC_ANGLE = 2f;
    public final float MAX_ARC_ANGLE = 217f;

    // Colors
    private int arcColor = Color.parseColor("#00E5FF");
    private int shadowColor = Color.parseColor("#8000E5FF");

    // Shadow config (defaults)
    private float shadowRadius = 18f;
    private float shadowDx = 0f;
    private float shadowDy = 0f;

    public ArcView(Context context) {
        super(context);
        init(null);
    }

    public ArcView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(attrs);
    }

    public ArcView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init(attrs);
    }

    private void init(AttributeSet attrs) {
        if (attrs != null) {
            TypedArray a = getContext().obtainStyledAttributes(attrs, R.styleable.ArcView);

            startAngle   = a.getFloat(R.styleable.ArcView_arcStartAngle, startAngle);
            sweepAngle   = a.getFloat(R.styleable.ArcView_arcSweepAngle, sweepAngle);
            strokeWidth  = a.getDimension(R.styleable.ArcView_arcStrokeWidth, strokeWidth);

            arcColor     = a.getColor(R.styleable.ArcView_arcColor, arcColor);
            shadowColor  = a.getColor(R.styleable.ArcView_arcShadowColor, shadowColor);

            // NEW: shadow radius + offsets (dimensions come in px)
            shadowRadius = a.getDimension(R.styleable.ArcView_arcShadowRadius, shadowRadius);
            shadowDx     = a.getDimension(R.styleable.ArcView_arcShadowDx, shadowDx);
            shadowDy     = a.getDimension(R.styleable.ArcView_arcShadowDy, shadowDy);

            a.recycle();
        }

        paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeCap(Paint.Cap.BUTT);      // flat ends
        paint.setStrokeWidth(strokeWidth);
        paint.setColor(arcColor);

        setLayerType(LAYER_TYPE_SOFTWARE, paint); // needed for shadow
        applyShadow();

        arcRect = new RectF();
    }

    private void applyShadow() {
        paint.setShadowLayer(shadowRadius, shadowDx, shadowDy, shadowColor);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);

        float padding = strokeWidth / 2f + shadowRadius; // extra room so shadow isn't cut
        arcRect.set(padding, padding, w - padding, h - padding);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        canvas.drawArc(arcRect, startAngle, sweepAngle, false, paint);
    }

    // --- Setters for runtime control ---

    public void setArcColor(int color) {
        this.arcColor = color;
        paint.setColor(color);
        invalidate();
    }

    public void setArcShadowColor(int color) {
        this.shadowColor = color;
        applyShadow();
        invalidate();
    }

    public void setArcShadowRadius(float radiusPx) {
        this.shadowRadius = radiusPx;
        applyShadow();
        requestLayout(); // padding depends on radius
        invalidate();
    }

    public void setArcShadowOffset(float dxPx, float dyPx) {
        this.shadowDx = dxPx;
        this.shadowDy = dyPx;
        applyShadow();
        invalidate();
    }

    public void setArcSweepAngle(float angle){
        this.sweepAngle = angle;
        invalidate();
    }
}