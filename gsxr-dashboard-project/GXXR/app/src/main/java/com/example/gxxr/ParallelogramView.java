package com.example.gxxr; // your package

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

public class ParallelogramView extends View {

    private Paint paint;
    private RectF rect;

    // Colors & shadow
    private int fillColor = Color.parseColor("#00E5FF");
    private int shadowColor = Color.parseColor("#8000E5FF");
    private float shadowRadius = 18f;
    private float shadowDx = 0f;
    private float shadowDy = 0f;
    private float cornerRadius = 24f;

    // Horizontal skew config
    private float skewAmount = 0.3f; // how strong the slant is
    private int skewDirection = 1;   // 1 = right, -1 = left, 0 = none

    // Scale config
    private float scale = 1f;        // 1 = full size, 0.5 = half, etc
    private int scaleDirection = 0;  // which side is the anchor

    private static final int SCALE_NONE   = 0;
    private static final int SCALE_LEFT   = 1;
    private static final int SCALE_RIGHT  = 2;
    private static final int SCALE_TOP    = 3;
    private static final int SCALE_BOTTOM = 4;

    public ParallelogramView(Context context) {
        super(context);
        init(null);
    }

    public ParallelogramView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(attrs);
    }

    public ParallelogramView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init(attrs);
    }

    private void init(AttributeSet attrs) {
        if (attrs != null) {
            TypedArray a = getContext().obtainStyledAttributes(attrs, R.styleable.ParallelogramView);

            fillColor    = a.getColor(R.styleable.ParallelogramView_paraFillColor, fillColor);
            shadowColor  = a.getColor(R.styleable.ParallelogramView_paraShadowColor, shadowColor);
            shadowRadius = a.getDimension(R.styleable.ParallelogramView_paraShadowRadius, shadowRadius);
            shadowDx     = a.getDimension(R.styleable.ParallelogramView_paraShadowDx, shadowDx);
            shadowDy     = a.getDimension(R.styleable.ParallelogramView_paraShadowDy, shadowDy);
            cornerRadius = a.getDimension(R.styleable.ParallelogramView_paraCornerRadius, cornerRadius);

            // skew (horizontal)
            skewDirection = a.getInt(R.styleable.ParallelogramView_paraSkewDirection, 1);
            skewAmount    = a.getFloat(R.styleable.ParallelogramView_paraSkewAmount, skewAmount);

            // scale
            scale          = a.getFloat(R.styleable.ParallelogramView_paraScale, 1f);
            scaleDirection = a.getInt(R.styleable.ParallelogramView_paraScaleDirection, SCALE_NONE);

            a.recycle();
        }

        paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(fillColor);

        setLayerType(LAYER_TYPE_SOFTWARE, paint);
        applyShadow();

        rect = new RectF();
    }

    private void applyShadow() {
        paint.setShadowLayer(shadowRadius, shadowDx, shadowDy, shadowColor);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);

        float basePad = shadowRadius + cornerRadius / 2f;

        // Extra horizontal room for skew: x' = x + kx * y
        float extraX = Math.abs(skewAmount) * h;

        float paddingX = basePad + extraX;
        float paddingY = basePad;

        rect.set(
                paddingX,
                paddingY,
                w - paddingX,
                h - paddingY
        );
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        canvas.save();

        // 1) Skew horizontally to get the parallelogram
        float kx = skewDirection * skewAmount;
        canvas.skew(kx, 0f);

        // 2) Apply scale around the chosen side (anchor)
        switch (scaleDirection) {
            case SCALE_LEFT:
                canvas.scale(scale, 1f, rect.left, rect.centerY());
                break;
            case SCALE_RIGHT:
                canvas.scale(scale, 1f, rect.right, rect.centerY());
                break;
            case SCALE_TOP:
                canvas.scale(1f, scale, rect.centerX(), rect.top);
                break;
            case SCALE_BOTTOM:
                canvas.scale(1f, scale, rect.centerX(), rect.bottom);
                break;
            case SCALE_NONE:
            default:
                // no scaling
                break;
        }

        // 3) Draw the rounded rect (appears as a bar skewed + scaled)
        canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint);

        canvas.restore();
    }

    // --------- Setters to control skew & growth direction in code ---------

    /** Horizontal skew direction: -1 = left, 0 = none, 1 = right */
    public void setSkewDirection(int direction) {
        if (direction > 0)      this.skewDirection = 1;
        else if (direction < 0) this.skewDirection = -1;
        else                    this.skewDirection = 0;
        requestLayout();
        invalidate();
    }

    public void setSkewAmount(float amount) {
        this.skewAmount = amount;
        requestLayout();
        invalidate();
    }

    /** Scale/growth amount (0..1 typical for progress) */
    public void setScaleAmount(float amount) {
        this.scale = amount;
        invalidate();
    }

    /** Grow from left edge */
    public void growFromLeft(float amount) {
        this.scaleDirection = SCALE_LEFT;
        this.scale = amount;
        invalidate();
    }

    /** Grow from right edge */
    public void growFromRight(float amount) {
        this.scaleDirection = SCALE_RIGHT;
        this.scale = amount;
        invalidate();
    }

    /** Grow from top edge */
    public void growFromTop(float amount) {
        this.scaleDirection = SCALE_TOP;
        this.scale = amount;
        invalidate();
    }

    /** Grow from bottom edge */
    public void growFromBottom(float amount) {
        this.scaleDirection = SCALE_BOTTOM;
        this.scale = amount;
        invalidate();
    }
}