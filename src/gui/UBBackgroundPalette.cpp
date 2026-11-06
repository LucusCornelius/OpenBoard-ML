#include <QToolButton>

#include "UBBackgroundPalette.h"

#include "gui/UBBackgroundManager.h"
#include "gui/UBFlowLayout.h"
#include "gui/UBMainWindow.h"


UBBackgroundPalette::UBBackgroundPalette(QWidget * parent)
     : UBActionPalette(parent)
{
    init();
    createActions();

    connect(UBApplication::boardController->backgroundManager(), &UBBackgroundManager::preferredBackgroundChanged,
            this, &UBBackgroundPalette::createActions);

    // keep palette in sync when the background is changed by shortcuts or page changes
    connect(UBApplication::boardController, &UBBoardController::backgroundChanged, this, [this]() {
        if (isVisible())
        {
            refresh();
        }
    });

    // shortcut-able actions to select one of the default backgrounds on the current page
    const auto mainWindow = UBApplication::mainWindow;

    connect(mainWindow->actionBackgroundBlank, &QAction::triggered, this, [this]() {
        selectBackground(UBBackgroundManager::blankBackground);
    });
    connect(mainWindow->actionBackgroundLined, &QAction::triggered, this, [this]() {
        selectBackground(UBBackgroundManager::linedBackground);
    });
    connect(mainWindow->actionBackgroundGrid1cm, &QAction::triggered, this, [this]() {
        selectBackground(UBBackgroundManager::grid1cmBackground);
    });
    connect(mainWindow->actionBackgroundGrid5mm, &QAction::triggered, this, [this]() {
        selectBackground(UBBackgroundManager::grid5mmBackground);
    });
}

void UBBackgroundPalette::init()
{
    setObjectName("BackgroundPalette");

    UBActionPalette::clearLayout();
    delete layout();

    m_customCloseProcessing = false;

    mButtonSize = QSize(32, 32);
    mAutoClose = false;
    mToolButtonStyle = Qt::ToolButtonIconOnly;
    mButtons.clear();

    mVLayout = new QVBoxLayout(this);
    mTopLayout = new UBFlowLayout{0, 0, 0};
    mTopLayout->setSizeConstraint(QLayout::SetMaximumSize);
    mBottomLayout = new QHBoxLayout();

    mButtonScrollArea = new QScrollArea{this};
    mButtonScrollArea->setObjectName("BackgroundPaletteScrollArea");
    mButtonScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mButtonScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mButtonScrollArea->setWidgetResizable(true);

    QWidget* bgWidget = new QWidget{mButtonScrollArea};
    bgWidget->setLayout(mTopLayout);
    mButtonScrollArea->setWidget(bgWidget);

    mVLayout->addWidget(mButtonScrollArea);
    mVLayout->addLayout(mBottomLayout);

    mSlider = new QSlider(Qt::Horizontal);

    mSlider->setMinimum(UBSettings::settings()->minCrossSize);
    mSlider->setMaximum(UBSettings::settings()->maxCrossSize);
    mSlider->setSingleStep(2);
    mSlider->setTracking(true); // valueChanged() is emitted during movement and not just upon releasing the slider
    connect(mSlider, &QSlider::valueChanged, this, &UBBackgroundPalette::sliderValueChanged);

    mSliderLabel = new QLabel(tr("Grid size"));
    mSliderLabel->setObjectName("BackgroundPaletteLabel");
    mSliderValueLabel = new QLabel;
    mSliderValueLabel->setObjectName("BackgroundPaletteLabel");
    mSliderValueLabel->setToolTip(tr("Spacing of the lines on screen at 100% zoom"));
    // reserve space for the longest text to avoid jumping of the layout
    mSliderValueLabel->setMinimumWidth(mSliderValueLabel->fontMetrics().horizontalAdvance("00.00 cm"));
    mLightDarkModeLabel = new QLabel(tr("Switch background color"));
    mLightDarkModeLabel->setObjectName("BackgroundPaletteLabel");

    mResetDefaultGridSizeButton = createPaletteButton(UBApplication::mainWindow->actionDefaultGridSize, this);
    mResetDefaultGridSizeButton->setFixedSize(32,32);
    mActions << UBApplication::mainWindow->actionDefaultGridSize;
    UBApplication::mainWindow->actionDefaultGridSize->setProperty("ungrouped", true); // don't add to action group

    connect(UBApplication::mainWindow->actionDefaultGridSize, &QAction::triggered, this, &UBBackgroundPalette::defaultBackgroundGridSize);

    mLightDarkModeSwitch = createPaletteButton(UBApplication::mainWindow->actionLightDarkMode, this);
    mLightDarkModeSwitch->setFixedSize(64,32);
    mLightDarkModeSwitch->setCheckable(true);
    mLightDarkModeSwitch->setObjectName("glassToggle"); // the switch icon shows the state
    mActions << UBApplication::mainWindow->actionLightDarkMode;
    UBApplication::mainWindow->actionLightDarkMode->setProperty("ungrouped", true); // don't add to action group

    connect(UBApplication::mainWindow->actionLightDarkMode, &QAction::triggered, this, &UBBackgroundPalette::toggleBackgroundColor);

    mBottomLayout->addSpacing(16);
    mBottomLayout->addWidget(mSliderLabel);
    mBottomLayout->addWidget(mSlider);
    mBottomLayout->addWidget(mSliderValueLabel);
    mBottomLayout->addWidget(mResetDefaultGridSizeButton);
    mBottomLayout->addSpacing(16);

    mBottomLayout->addWidget(mLightDarkModeLabel);
    mBottomLayout->addWidget(mLightDarkModeSwitch);
    mBottomLayout->addSpacing(16);

    UBBackgroundPalette::updateLayout();
}

void UBBackgroundPalette::createActions()
{
    // first delete everything from the top layout
    while(!mTopLayout->isEmpty())
    {
        QLayoutItem* pItem = mTopLayout->itemAt(0);
        QWidget* pW = pItem->widget();
        mTopLayout->removeItem(pItem);
        delete pItem;
        mTopLayout->removeWidget(pW);

        QAction* action = removePaletteButton(dynamic_cast<UBActionPaletteButton*>(pW));

        delete pW;
        delete action;
    }

    // then add actions for the backgrounds from the background manager
    const QList<const UBBackgroundRuling*> backgrounds = UBApplication::boardController->backgroundManager()->preferredBackgrounds();
    const auto dark = false;  // starting value, updated later with updateActions()
    const auto bgManager = UBApplication::boardController->backgroundManager();

    for (int i = 0; i < backgrounds.size(); ++i)
    {
        const auto background = backgrounds.at(i);
        auto action = bgManager->backgroundAction(*background, dark);
        UBBackgroundPalette::addAction(action);

        connect(action, &QAction::triggered, this, &UBBackgroundPalette::changeBackground);
    }

    actionChanged();
    updateActions();
}

void UBBackgroundPalette::updateActions()
{
    const auto scene = UBApplication::boardController->activeScene();

    if (!scene)
    {
        return;
    }

    const auto dark = scene->isDarkBackground();
    UBApplication::mainWindow->actionLightDarkMode->setChecked(dark);
    QUuid uuid;

    if (scene->background())
    {
        uuid = scene->background()->uuid();
    }

    for (auto& action : mActions)
    {
        QUuid actionUuid = action->property("uuid").toUuid();

        if (!actionUuid.isNull())
        {
            UBApplication::boardController->backgroundManager()->updateAction(action, dark);
            action->setChecked(uuid == actionUuid);
        }
    }
}

void UBBackgroundPalette::addAction(QAction* action)
{
    UBActionPaletteButton* button = createPaletteButton(action, this);

    mTopLayout->addWidget(button);
    mActions << action;

    const auto size = action->icon().availableSizes().at(0);

    // count the background buttons, they have an uuid
    const auto buttonCount = std::count_if(mActions.cbegin(), mActions.cend(), [](const QAction* a) {
        return !a->property("uuid").toUuid().isNull();
    });

    // one row if all buttons fit into it, else make part of the second row visible
    const int rowHeight = std::max(size.height(), button->sizeHint().height()) + 2 * mButtonScrollArea->frameWidth() + 6;
    const int height = buttonCount <= 6 ? rowHeight : size.height() * 3 / 2;
    mButtonScrollArea->setVerticalScrollBarPolicy(buttonCount <= 6 ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAlwaysOn);

    if (mButtonScrollArea->height() != height)
    {
        mButtonScrollArea->setFixedHeight(height);
        // account for the width of the scrollbar
        mButtonScrollArea->setFixedWidth(6 * size.width() + 40);
    }
}

void UBBackgroundPalette::setActions(QList<QAction*> actions)
{
    mMapActionToButton.clear();

    foreach(QAction* action, actions)
    {
        addAction(action);
    }

    actionChanged();
}

void UBBackgroundPalette::updateLayout()
{
    const int margin = (mToolButtonStyle == Qt::ToolButtonIconOnly ? sLayoutContentMargin / 2 : sLayoutContentMargin) + border();

    // keep the close button of the glass panel free from the content
    const int top = isClosable() ? qMax(margin, closeButtonTop()) : margin;

    mVLayout->setContentsMargins(margin, top, margin, margin);

   update();
}

void UBBackgroundPalette::clearLayout()
{
    while(!mTopLayout->isEmpty()) {
        QLayoutItem* pItem = mTopLayout->itemAt(0);
        QWidget* pW = pItem->widget();
        mTopLayout->removeItem(pItem);
        delete pItem;
        mTopLayout->removeWidget(pW);
        delete pW;
    }

    delete mTopLayout;

    while(!mBottomLayout->isEmpty()) {
        QLayoutItem* pItem = mBottomLayout->itemAt(0);
        QWidget* pW = pItem->widget();
        mBottomLayout->removeItem(pItem);
        delete pItem;
        mBottomLayout->removeWidget(pW);
        delete pW;
    }

    delete mBottomLayout;

    delete mVLayout;

    mActions.clear();
    mButtons.clear();
}

void UBBackgroundPalette::showEvent(QShowEvent* event)
{
    const auto scene = UBApplication::boardController->activeScene();
    mIsDark = scene->isDarkBackground();
    mLightDarkModeSwitch->setChecked(mIsDark);
    mBackground = scene->background();

    refresh();

    QWidget::showEvent(event);
}

void UBBackgroundPalette::sliderValueChanged(int value)
{
    UBApplication::boardController->activeScene()->setBackgroundGridSize(value);
    UBSettings::settings()->crossSize = value; // since this function is called (indirectly, by refresh) when we switch scenes, the settings will always have the current scene's cross size.

    UBApplication::boardController->activeScene()->setModified(true);
    updateGridSizeLabel();
}

void UBBackgroundPalette::defaultBackgroundGridSize()
{
    updateSliderRange();
    const QSignalBlocker blocker{mSlider};
    mSlider->setValue(UBSettings::settings()->defaultCrossSize);
    sliderValueChanged(UBSettings::settings()->defaultCrossSize);
}

void UBBackgroundPalette::toggleBackgroundColor(bool checked)
{
    Q_UNUSED(checked)

    const auto scene = UBApplication::boardController->activeScene();

    if (!scene)
    {
        return;
    }

    // use the state of the active scene, the palette may be outdated when triggered by a shortcut
    mIsDark = !scene->isDarkBackground();
    mBackground = scene->background();
    UBApplication::boardController->setBackground(mIsDark, mBackground);
    backgroundChanged();
}

void UBBackgroundPalette::changeBackground(bool checked)
{
    if (checked)
    {
        QAction* action = dynamic_cast<QAction*>(sender());

        if (action)
        {
            selectBackground(action->property("uuid").toUuid());
        }
    }
}

void UBBackgroundPalette::selectBackground(const QUuid& uuid)
{
    const auto scene = UBApplication::boardController->activeScene();
    const auto background = UBApplication::boardController->backgroundManager()->background(uuid);

    if (!scene || !background)
    {
        return;
    }

    mIsDark = scene->isDarkBackground();
    mBackground = background;
    UBApplication::boardController->setBackground(mIsDark, mBackground);
    backgroundChanged();
    updateGridSizeLabel();
}

void UBBackgroundPalette::backgroundChanged()
{
    updateActions();
}

void UBBackgroundPalette::refresh()
{
    backgroundChanged();
    updateSliderRange();

    {
        const QSignalBlocker blocker{mSlider};
        mSlider->setValue(UBApplication::boardController->activeScene()->backgroundGridSize());
    }

    // new pages use the grid size of the current page
    UBSettings::settings()->crossSize = UBApplication::boardController->activeScene()->backgroundGridSize();

    updateGridSizeLabel();
}

void UBBackgroundPalette::updateGridSizeLabel()
{
    const auto scene = UBApplication::boardController->activeScene();
    const int defaultGridSize = UBSettings::settings()->defaultCrossSize;

    if (!scene || defaultGridSize <= 0)
    {
        mSliderValueLabel->clear();
        return;
    }

    // The grid size is the number of scene pixels per ruling centimetre, the default grid size
    // is calibrated to show 1 cm on screen at 100% zoom. Show the spacing of the ruling lines.
    const auto background = scene->background();
    double spacingMm = background ? background->primarySpacing() : 0.;

    if (spacingMm <= 0.)
    {
        spacingMm = 10.;
    }

    const double spacingCm = spacingMm / 10. * scene->backgroundGridSize() / defaultGridSize;
    mSliderValueLabel->setText(tr("%1 cm").arg(spacingCm, 0, 'f', spacingCm < 0.5 ? 2 : 1));
}

void UBBackgroundPalette::updateSliderRange()
{
    // make sure the calibrated default and the current value are always reachable
    const int defaultGridSize = UBSettings::settings()->defaultCrossSize;
    const auto scene = UBApplication::boardController->activeScene();
    const int current = scene ? scene->backgroundGridSize() : defaultGridSize;

    const QSignalBlocker blocker{mSlider};
    mSlider->setMinimum(std::min({UBSettings::settings()->minCrossSize, defaultGridSize / 2, current}));
    mSlider->setMaximum(std::max({UBSettings::settings()->maxCrossSize, defaultGridSize * 3, current}));
}
