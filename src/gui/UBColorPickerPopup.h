/*
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef UBCOLORPICKERPOPUP_H
#define UBCOLORPICKERPOPUP_H

#include <QColor>
#include <QDialog>
#include <QFrame>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QWidget>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QToolButton;
class UBMainWindow;


/**
 * Hue (horizontal) / saturation (vertical) selection area.
 */
class UBHueSaturationArea : public QWidget
{
    Q_OBJECT

    public:
        explicit UBHueSaturationArea(QWidget* parent = nullptr);

        void setHueSaturation(qreal hue, qreal saturation);
        QSize sizeHint() const override;
        QSize minimumSizeHint() const override;

    signals:
        void hueSaturationChanged(qreal hue, qreal saturation);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;

    private:
        void pick(const QPointF& pos);

        QImage mImage;
        qreal mHue{0};
        qreal mSaturation{1};
};


/**
 * Vertical value (brightness) slider showing the current hue/saturation.
 */
class UBColorValueBar : public QWidget
{
    Q_OBJECT

    public:
        explicit UBColorValueBar(QWidget* parent = nullptr);

        void setColor(qreal hue, qreal saturation, qreal value);
        QSize sizeHint() const override;
        QSize minimumSizeHint() const override;

    signals:
        void valueChanged(qreal value);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;

    private:
        void pick(const QPointF& pos);

        qreal mHue{0};
        qreal mSaturation{0};
        qreal mValue{0};
};


/**
 * A row of quick-access colors (rainbow plus greys).
 */
class UBColorSwatchRow : public QWidget
{
    Q_OBJECT

    public:
        explicit UBColorSwatchRow(QWidget* parent = nullptr);

        void setColors(const QList<QColor>& colors);
        QSize sizeHint() const override;
        QSize minimumSizeHint() const override;

    signals:
        void colorClicked(const QColor& color);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;

    private:
        QRectF cellRect(int index) const;

        QList<QColor> mColors;
};


/**
 * Shows the previous and the new color on the page background.
 */
class UBColorPreview : public QWidget
{
    Q_OBJECT

    public:
        explicit UBColorPreview(QWidget* parent = nullptr);

        void setColors(const QColor& oldColor, const QColor& newColor);
        void setBackground(const QColor& background);
        void setAlpha(qreal alpha);
        QSize sizeHint() const override;

    protected:
        void paintEvent(QPaintEvent* event) override;

    private:
        QColor mOldColor;
        QColor mNewColor;
        QColor mBackground{Qt::white};
        qreal mAlpha{1.0};
};


/**
 * Touch/stylus friendly full spectrum color picker: hue/saturation area,
 * value bar, quick colors, preview and hex/RGB input.
 */
class UBColorPickerWidget : public QWidget
{
    Q_OBJECT

    public:
        explicit UBColorPickerWidget(QWidget* parent = nullptr);

        QColor color() const;
        void setColor(const QColor& color);
        void setInitialColor(const QColor& color);
        void setPreviewBackground(const QColor& background);
        void setPreviewAlpha(qreal alpha);

    signals:
        void colorChanged(const QColor& color);

    private slots:
        void hueSaturationPicked(qreal hue, qreal saturation);
        void valuePicked(qreal value);
        void hexEdited();
        void rgbEdited();

    private:
        void updateControls();

        UBHueSaturationArea* mArea{nullptr};
        UBColorValueBar* mValueBar{nullptr};
        UBColorSwatchRow* mQuickColors{nullptr};
        UBColorPreview* mPreview{nullptr};
        QLineEdit* mHexEdit{nullptr};
        QSpinBox* mRed{nullptr};
        QSpinBox* mGreen{nullptr};
        QSpinBox* mBlue{nullptr};

        qreal mHue{0};
        qreal mSaturation{0};
        qreal mValue{0};
        QColor mInitialColor;
        bool mUpdating{false};
};


/**
 * Modal dialog wrapping UBColorPickerWidget, with a QColorDialog fallback.
 */
class UBColorPickerDialog : public QDialog
{
    Q_OBJECT

    public:
        UBColorPickerDialog(const QColor& initial, QWidget* parent = nullptr, const QString& title = QString());

        QColor color() const;
        UBColorPickerWidget* picker() const { return mPicker; }

        static QColor getColor(const QColor& initial, QWidget* parent, const QString& title,
                               const QColor& background = QColor(Qt::white), qreal alpha = 1.0);

    private slots:
        void moreColors();

    private:
        UBColorPickerWidget* mPicker{nullptr};
};


/**
 * Compact popup opened from the board toolbar to pick any color for the
 * current tool (pen or marker), add it to the active palette and switch palettes.
 */
class UBColorPickerPopup : public QFrame
{
    Q_OBJECT

    public:
        // swatchIndex >= 0 when opened from a toolbar swatch (right click / long press)
        static void showPopup(QWidget* anchor = nullptr, int swatchIndex = -1);

        // Wire the toolbar actions; to be called once the board toolbar is set up.
        static void setupIntegration(UBMainWindow* mainWindow, const QList<QAction*>& colorActions);

        // Open the picker on right click / long press of the swatch buttons of the given actions.
        static void installSwatchHandlers(const QList<QAction*>& colorActions);

    protected:
        void keyPressEvent(QKeyEvent* event) override;

    private slots:
        void useColor();
        void addToPalette();
        void setSwatch();
        void moreColors();
        void managePalettes();
        void paletteActivated(int index);
        void refreshPalettes();

    private:
        UBColorPickerPopup(int swatchIndex, QWidget* parent = nullptr);

        void placeNear(QWidget* anchor);
        static QWidget* dialogParent();

        static QPointer<UBColorPickerPopup> sInstance;

        int mSwatchIndex{-1};
        QLabel* mTitle{nullptr};
        QComboBox* mPaletteCombo{nullptr};
        UBColorPickerWidget* mPicker{nullptr};
};


/**
 * Event filter for the toolbar color swatches: right click or long press
 * opens the color picker popup for that swatch.
 */
class UBColorSwatchEventFilter : public QObject
{
    Q_OBJECT

    public:
        static UBColorSwatchEventFilter* instance();

        void addColorActions(const QList<QAction*>& actions);
        void watch(QToolButton* button);

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override;

    private slots:
        void longPressTimeout();

    private:
        explicit UBColorSwatchEventFilter(QObject* parent = nullptr);

        int swatchIndex(QToolButton* button) const;

        QList<QAction*> mColorActions;
        QTimer* mTimer{nullptr};
        QPointer<QToolButton> mPressedButton;
        bool mLongPressed{false};
};

#endif // UBCOLORPICKERPOPUP_H
