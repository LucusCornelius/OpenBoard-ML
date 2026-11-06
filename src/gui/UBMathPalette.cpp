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

#include "UBMathPalette.h"

#include <QButtonGroup>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "board/UBBoardController.h"
#include "board/UBBoardView.h"
#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "gui/UBInkWriter.h"
#include "gui/UBGlassStyle.h"

#include "core/memcheck.h"

namespace
{
    const int sColumns = 10;
    const int sButtonSize = 36;
    const int sIconSize = 30;
}

UBMathPalette::UBMathPalette(QWidget* parent)
    : UBFloatingPalette(Qt::TopLeftCorner, parent)
{
    setObjectName("MathPalette");
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);

    // the palette keeps the position where the user dropped it
    setCustomPosition(true);

    mWriter = new UBInkWriter(this);
    connect(mWriter, &UBInkWriter::placementStarted, this, &UBMathPalette::placementStarted);
    connect(mWriter, &UBInkWriter::placementFinished, this, &UBMathPalette::placementFinished);

    const int b = border();
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(b + 12, b + 8, b + 12, b + 10);
    mainLayout->setSpacing(8);

    // tab row: a segmented capsule, with room for the close button of the floating palette (top left corner)
    QHBoxLayout* tabRow = new QHBoxLayout();
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->setSpacing(0);
    tabRow->addSpacing(closeButtonMargin() + 22 + 8 - (b + 12));

    QFrame* segmented = new QFrame(this);
    segmented->setObjectName("glassSegmented");
    QHBoxLayout* tabLayout = new QHBoxLayout(segmented);
    tabLayout->setContentsMargins(3, 3, 3, 3);
    tabLayout->setSpacing(2);
    tabRow->addWidget(segmented);

    mPageButtons = new QButtonGroup(this);
    mPageButtons->setExclusive(true);
    mPages = new QStackedWidget(this);
    mPages->setFocusPolicy(Qt::NoFocus);

    const QList<MathPage> pages = buildPages();

    for (int p = 0; p < pages.size(); ++p)
    {
        const MathPage& page = pages.at(p);

        QToolButton* tab = new QToolButton(segmented);
        tab->setObjectName("glassSegment");
        tab->setText(page.title);
        tab->setCheckable(true);
        tab->setFocusPolicy(Qt::NoFocus);
        tab->setToolButtonStyle(Qt::ToolButtonTextOnly);
        mPageButtons->addButton(tab, p);
        tabLayout->addWidget(tab);

        QWidget* pageWidget = new QWidget(mPages);
        pageWidget->setFocusPolicy(Qt::NoFocus);
        QGridLayout* grid = new QGridLayout(pageWidget);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        for (int i = 0; i < page.templates.size(); ++i)
        {
            grid->addWidget(createTemplateButton(page.templates.at(i), pageWidget), i / sColumns, i % sColumns);
        }

        mPages->addWidget(pageWidget);
    }

    tabRow->addStretch(1);
    mainLayout->addLayout(tabRow);
    mainLayout->addWidget(mPages);

    // bottom row: size and ink text
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(8);

    QLabel* sizeTitle = new QLabel(tr("Size"), this);
    bottomLayout->addWidget(sizeTitle);

    mSizeSlider = new QSlider(Qt::Horizontal, this);
    mSizeSlider->setFocusPolicy(Qt::NoFocus);
    mSizeSlider->setRange(2, 12); // half centimetres: 1 to 6 cm
    mSizeSlider->setPageStep(2);
    mSizeSlider->setFixedWidth(100);
    mSizeSlider->setToolTip(tr("Height of capital letters and digits on the board"));
    bottomLayout->addWidget(mSizeSlider);

    mSizeLabel = new QLabel(this);
    mSizeLabel->setMinimumWidth(QFontMetrics(mSizeLabel->font()).horizontalAdvance(tr("%1 cm").arg(QLocale().toString(5.5, 'f', 1))) + 4);
    bottomLayout->addWidget(mSizeLabel);
    bottomLayout->addSpacing(8);

    mInkText = new QLineEdit(this);
    mInkText->setFocusPolicy(Qt::ClickFocus);
    mInkText->setClearButtonEnabled(true);
    mInkText->setPlaceholderText(tr("Ink text, e.g. f(x) = x^2 + 1/(x-1)"));
    mInkText->setToolTip(tr("Type a formula and press Enter, then tap the board to write it by hand.\n"
                            "x^2 superscript, x_1 subscript, a/b fraction, sqrt(x), int_a^b, sum_(n=1)^inf,\n"
                            "lim_(x->0), vec(v), pi, alpha, ..., -> => <= >= != +-, # leaves an empty box"));
    connect(mInkText, &QLineEdit::returnPressed, this, &UBMathPalette::inkTextEntered);
    bottomLayout->addWidget(mInkText, 1);

    QToolButton* writeButton = new QToolButton(this);
    writeButton->setObjectName("glassButton");
    writeButton->setFocusPolicy(Qt::NoFocus);
    writeButton->setText(tr("Write"));
    writeButton->setToolTip(tr("Write the ink text on the board"));
    connect(writeButton, &QToolButton::clicked, this, &UBMathPalette::inkTextEntered);
    bottomLayout->addWidget(writeButton);

    mainLayout->addLayout(bottomLayout);

    mHint = new QLabel(this);
    mHint->setObjectName("glassHint");
    mHint->setWordWrap(true);
    mainLayout->addWidget(mHint);
    showHint();

    const qreal sizeCm = qBound(1.0, UBSettings::settings()->boardMathInkSizeCm->get().toDouble(), 6.0);
    mSizeSlider->setValue(qRound(sizeCm * 2));
    sizeChanged(mSizeSlider->value());
    connect(mSizeSlider, &QSlider::valueChanged, this, &UBMathPalette::sizeChanged);

    connect(mPageButtons, &QButtonGroup::idClicked, this, &UBMathPalette::pageSelected);

    int page = UBSettings::settings()->boardMathPaletteTab->get().toInt();

    if (page < 0 || page >= mPages->count())
    {
        page = 0;
    }

    mPageButtons->button(page)->setChecked(true);
    mPages->setCurrentIndex(page);
    updatePageSize();

    const QVariant position = UBSettings::settings()->boardMathPalettePosition->get();

    if (position.isValid() && position.canConvert<QPoint>())
    {
        mPreferredPos = position.toPoint();
        mHasPreferredPos = true;
    }

    // build the close button last so that it is on top of the layout
    setClosable(true);
    resize(sizeHint());

    if (parent)
    {
        parent->installEventFilter(this);
    }
}

UBMathPalette::~UBMathPalette()
{
    // NOOP
}

QList<UBMathPalette::MathPage> UBMathPalette::buildPages()
{
    QList<MathPage> pages;

    // sources use the ink text syntax of UBMathInkLayout, # is an empty box to write into
    MathPage calculus;
    calculus.title = tr("Calculus");
    calculus.templates = {
        {"sqrt(#)", tr("Square root")},
        {"#/#", tr("Fraction")},
        {"int_#^#", tr("Integral with bounds")},
        {"int", tr("Integral")},
        {"iint", tr("Double integral")},
        {"oint", tr("Contour integral")},
        {"sum_#^#", tr("Sum with bounds")},
        {"prod_#^#", tr("Product with bounds")},
        {"lim_(x->a)", tr("Limit for x towards a")},
        {"lim_(x->inf)", tr("Limit for x towards infinity")},
        {"d/(dx)", tr("Derivative d/dx")},
        {"partial/(partial x)", tr("Partial derivative")},
        {"f(x)", tr("Function f(x)")},
        {"f'(x)", tr("Derivative f′(x)")},
        {"dx", tr("dx")},
        {"dy", tr("dy")},
        {"dt", tr("dt")},
        {"inf", tr("Infinity")},
        {"partial", tr("Partial")},
        {"nabla", tr("Nabla")},
        {"Delta", tr("Delta")},
        {"vec(#)", tr("Vector arrow")},
        {"vec(v)", tr("Vector v")}
    };

    MathPage algebra;
    algebra.title = tr("Algebra");
    algebra.templates = {
        {"x^2", tr("x squared")},
        {"x^n", tr("x to the power n")},
        {"x_1", tr("x with index 1")},
        {"x_n", tr("x with index n")},
        {"|#|", tr("Absolute value")},
        {"(", tr("Left parenthesis")},
        {")", tr("Right parenthesis")},
        {"[", tr("Left bracket")},
        {"]", tr("Right bracket")},
        {"\\{", tr("Left brace")},
        {"\\}", tr("Right brace")},
        {"|", tr("Vertical bar")},
        {"±", tr("Plus or minus")},
        {"×", tr("Times")},
        {"÷", tr("Divided by")},
        {"·", tr("Dot")},
        {"≠", tr("Not equal")},
        {"≈", tr("Approximately equal")},
        {"≡", tr("Identical")},
        {"≤", tr("Less than or equal")},
        {"≥", tr("Greater than or equal")},
        {"→", tr("Arrow")},
        {"⇒", tr("Implies")},
        {"⇔", tr("If and only if")},
        {"∈", tr("Element of")},
        {"∉", tr("Not an element of")},
        {"⊂", tr("Subset")},
        {"∪", tr("Union")},
        {"∩", tr("Intersection")},
        {"∅", tr("Empty set")},
        {"∀", tr("For all")},
        {"∃", tr("There exists")},
        {"°", tr("Degree")},
        {"ℕ", tr("Natural numbers")},
        {"ℤ", tr("Integers")},
        {"ℚ", tr("Rational numbers")},
        {"ℝ", tr("Real numbers")}
    };

    MathPage greek;
    greek.title = tr("Greek");
    const QStringList greekLetters = {
        "alpha", "beta", "gamma", "delta", "epsilon", "theta", "lambda", "mu", "pi", "rho",
        "sigma", "tau", "phi", "chi", "psi", "omega", "Gamma", "Delta", "Theta", "Lambda",
        "Sigma", "Pi", "Phi", "Omega"
    };

    for (const QString& letter : greekLetters)
    {
        greek.templates << MathTemplate{letter, letter};
    }

    MathPage functions;
    functions.title = tr("Functions");
    functions.templates = {
        {"sin", tr("Sine")},
        {"cos", tr("Cosine")},
        {"tan", tr("Tangent")},
        {"ln", tr("Natural logarithm")},
        {"log", tr("Logarithm")},
        {"log_#", tr("Logarithm with base")},
        {"e^x", tr("Exponential")},
        {"e^#", tr("e to the power")},
        {"f(x)", tr("Function f(x)")},
        {"g(x)", tr("Function g(x)")}
    };

    for (const QString& c : QStringList{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                                        "x", "y", "z", "a", "b", "c", "n", "k", "t", "A"})
    {
        functions.templates << MathTemplate{c, c};
    }

    pages << calculus << algebra << greek << functions;
    return pages;
}

QPixmap UBMathPalette::formulaPixmap(const UBMathInkLayout::Formula& formula, const QSize& size,
                                     const QColor& color, qreal devicePixelRatio)
{
    QPixmap pixmap(size * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);

    const QList<QPolygonF> ink = UBInkWriter::inkStrokes(formula);
    QRectF bounds;

    for (const QPolygonF& stroke : ink)
    {
        bounds = bounds.isNull() ? stroke.boundingRect() : bounds.united(stroke.boundingRect());
    }

    for (const QRectF& rect : formula.placeholders)
    {
        bounds = bounds.isNull() ? rect : bounds.united(rect);
    }

    if (bounds.isEmpty() && bounds.isNull())
    {
        return pixmap;
    }

    const qreal margin = 1;
    const qreal available = qMin(size.width(), size.height()) - 2 * margin;
    // single small symbols are not blown up: at most 44 px per em
    const qreal scale = qMin(44.0, available / qMax(qMax(bounds.width(), bounds.height()), 0.01));

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(size.width() / 2.0, size.height() / 2.0);
    painter.scale(scale, scale);
    painter.translate(-bounds.center());

    QColor placeholderColor = color;
    placeholderColor.setAlphaF(0.7);
    QPen placeholderPen(placeholderColor, 1.0 / scale, Qt::DashLine);
    painter.setPen(placeholderPen);
    painter.setBrush(Qt::NoBrush);

    for (const QRectF& rect : formula.placeholders)
    {
        painter.drawRect(rect.adjusted(0.03, 0.03, -0.03, -0.03));
    }

    QPen inkPen(color, 2.0 / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(inkPen);

    for (const QPolygonF& stroke : ink)
    {
        painter.drawPolyline(stroke);
    }

    return pixmap;
}

QToolButton* UBMathPalette::createTemplateButton(const MathTemplate& tmpl, QWidget* parent)
{
    QToolButton* button = new QToolButton(parent);
    button->setObjectName("glassButton");
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(sButtonSize, sButtonSize);
    button->setCheckable(true);
    button->setToolTip(tmpl.toolTip);

    QString error;
    const UBMathInkLayout::Formula formula = UBMathInkLayout::layout(tmpl.source, &error);

    if (!error.isEmpty())
    {
        qWarning() << "UBMathPalette: cannot lay out" << tmpl.source << error;
    }

    // monochrome mask, tinted with the glyph colour of the glass material below the palette
    const QImage mask = formulaPixmap(formula, QSize(sIconSize, sIconSize), Qt::black, qMax<qreal>(2.0, devicePixelRatioF())).toImage();
    button->setIcon(UBGlassStyle::tintedImageIcon(mask));
    button->setIconSize(QSize(sIconSize, sIconSize));

    button->setProperty("formulaIndex", mFormulas.size());
    mFormulas << formula;

    connect(button, &QToolButton::clicked, this, &UBMathPalette::templateButtonClicked);

    return button;
}

int UBMathPalette::border()
{
    // room for the drop shadow of the glass
    return UBGlassStyle::shadowMargin;
}

void UBMathPalette::setPaletteVisible(bool visible)
{
    if (visible != isVisible())
    {
        setVisible(visible);
    }

    if (visible)
    {
        raise();
    }

    UBSettings::settings()->boardShowMathPalette->set(visible);
}

void UBMathPalette::onCloseButtonClicked()
{
    setPaletteVisible(false);
    emit closed();
}

void UBMathPalette::mouseReleaseEvent(QMouseEvent* event)
{
    const bool wasMoving = mIsMoving;

    UBFloatingPalette::mouseReleaseEvent(event);

    if (wasMoving)
    {
        mPreferredPos = pos();
        mHasPreferredPos = true;
        UBSettings::settings()->boardMathPalettePosition->set(mPreferredPos);
    }
}

void UBMathPalette::showEvent(QShowEvent* event)
{
    placeInsideParent();
    UBFloatingPalette::showEvent(event);
}

void UBMathPalette::hideEvent(QHideEvent* event)
{
    mWriter->cancelPlacement();
    UBFloatingPalette::hideEvent(event);
}

bool UBMathPalette::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
    {
        placeInsideParent();
    }

    return UBFloatingPalette::eventFilter(watched, event);
}

void UBMathPalette::placeInsideParent()
{
    QWidget* parent = parentWidget();

    if (!parent)
    {
        return;
    }

    resize(sizeHint());

    QPoint target = mPreferredPos;

    if (!mHasPreferredPos)
    {
        // default: centered near the bottom of the board
        target = QPoint((parent->width() - width()) / 2, parent->height() - height() - 100);
    }

    moveInsideParent(target);
}

void UBMathPalette::pageSelected(int index)
{
    if (index >= 0 && index < mPages->count())
    {
        // keep the tab row where it is, so that the tabs do not jump away from the finger
        const QPoint topLeft = pos();

        mPages->setCurrentIndex(index);
        UBSettings::settings()->boardMathPaletteTab->set(index);
        updatePageSize();

        if (isVisible())
        {
            moveInsideParent(topLeft);
        }
    }
}

void UBMathPalette::updatePageSize()
{
    // let the palette fit the current page only: hidden pages are ignored for the height
    for (int i = 0; i < mPages->count(); ++i)
    {
        QWidget* page = mPages->widget(i);
        page->setSizePolicy(QSizePolicy::Preferred, i == mPages->currentIndex() ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    }

    mPages->updateGeometry();

    if (layout())
    {
        layout()->activate();
    }

    resize(sizeHint());
}

void UBMathPalette::showHint(const QString& text)
{
    mHint->setText(text.isEmpty() ? tr("Tap a symbol, then tap the board where it should be written.") : text);
}

void UBMathPalette::templateButtonClicked()
{
    QToolButton* button = qobject_cast<QToolButton*>(sender());

    if (!button)
    {
        return;
    }

    if (mWriter->isPlacing() && mActiveButton == button)
    {
        // second tap on the same symbol: cancel
        mWriter->cancelPlacement();
        return;
    }

    const int index = button->property("formulaIndex").toInt();

    if (index < 0 || index >= mFormulas.size() || mFormulas.at(index).isEmpty())
    {
        button->setChecked(false);
        return;
    }

    mWriter->cancelPlacement();
    mActiveButton = button;
    button->setChecked(true);
    mWriter->beginPlacement(mFormulas.at(index));

    if (!mWriter->isPlacing())
    {
        button->setChecked(false);
        mActiveButton = nullptr;
    }
}

void UBMathPalette::inkTextEntered()
{
    const QString text = mInkText->text().trimmed();

    if (text.isEmpty())
    {
        return;
    }

    QString error;
    const UBMathInkLayout::Formula formula = UBMathInkLayout::layout(text, &error);

    if (!error.isEmpty() || formula.isEmpty())
    {
        showHint(error.isEmpty() ? tr("Nothing to write.") : error);
        return;
    }

    mWriter->cancelPlacement();
    mInkText->selectAll();

    // give the keyboard back to the board
    if (UBApplication::boardController && UBApplication::boardController->controlView())
    {
        UBApplication::boardController->controlView()->setFocus();
    }

    mWriter->beginPlacement(formula);
}

void UBMathPalette::sizeChanged(int halfCentimetres)
{
    const qreal sizeCm = halfCentimetres / 2.0;
    mSizeLabel->setText(tr("%1 cm").arg(QLocale().toString(sizeCm, 'f', halfCentimetres % 2 ? 1 : 0)));
    UBSettings::settings()->boardMathInkSizeCm->set(sizeCm);
}

void UBMathPalette::placementStarted()
{
    showHint(tr("Tap the board where it should be written (Esc cancels)."));
}

void UBMathPalette::placementFinished(bool written)
{
    Q_UNUSED(written);

    if (mActiveButton)
    {
        mActiveButton->setChecked(false);
    }

    mActiveButton = nullptr;
    showHint();
}
