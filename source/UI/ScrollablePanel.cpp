#include "ScrollablePanel.h"

ScrollablePanel::ScrollablePanel() {
    this->viewport.setViewedComponent(&this->contentComponent, false);
    this->viewport.setScrollBarsShown(true, false);
    this->addAndMakeVisible(this->viewport);
}

void ScrollablePanel::layoutViewportToBounds() {
    this->viewport.setBounds(this->getLocalBounds());
}
