#include "clickoverlay.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QMouseEvent>

ClickOverlay::ClickOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet("background-color: black;");

    auto *label = new QLabel(tr("Click to select video"), this);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(
        "color: white;"
        "font-size: 18px;"
        "font-weight: 500;"
        );

    auto *layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(label);
    layout->addStretch();
}

void ClickOverlay::mousePressEvent(QMouseEvent *event)
{
    qDebug() << "Overlay clicked";
    emit clicked();
}
