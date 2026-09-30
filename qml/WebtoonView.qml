import QtQuick
import QtQuick.Controls

/// 条漫（Webtoon）连续滚动阅读视图
///
/// 纵向无缝拼接所有页面，滚轮/拖拽滚动，翻页键跳转整页。
/// 仅渲染可视区域附近的页面（delegate 复用 ListView）。
Item {
    id: webtoon

    property var controller: null
    /// 页面显示宽度（0 表示自适应容器宽度）
    property int pageWidth: 0
    /// 页面之间的间距
    property int pageSpacing: 0
    /// 向下滚动一页的步长
    property int pageStride: 1

    implicitWidth: 800
    implicitHeight: 600

    function scrollToPage(index) {
        if (!controller || controller.pageCount <= 0)
            return;
        list.positionViewAtIndex(qBound(0, index, controller.pageCount - 1),
                                  ListView.Center)
    }

    function currentVisiblePage() {
        if (!controller || controller.pageCount <= 0)
            return 0
        return qBound(0, Math.round(list.contentY / Math.max(1, list.currentIndex + 1)),
                      controller.pageCount - 1)
    }

    Rectangle {
        anchors.fill: parent
        color: "#141414"
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        spacing: webtoon.pageSpacing
        // 页面高度未知，使用固定高度占位；实际以缩略图宽度等比缩放
        model: webtoon.controller ? webtoon.controller.pageCount : 0
        boundsBehavior: Flickable.StopAtBounds
        // 性能关键：只创建可视区域附近的委托
        cacheBuffer: webtoon.height

        delegate: Item {
            id: wrapper
            width: list.width
            height: pageImg.height

            Image {
                id: pageImg
                width: webtoon.pageWidth > 0 ? webtoon.pageWidth : wrapper.width
                // 首帧用预估高度，加载完成后由 onStatusChanged 校正
                height: width * 1.4
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                smooth: true
                // 距离可视区域较远时降级为低优先级加载
                source: "image://comicpage/" + index

                onStatusChanged: {
                    if (status === Image.Ready && sourceSize.height > 0
                            && width > 0) {
                        // 按真实宽高比修正高度，避免留白
                        height = Math.max(1, Math.round(width * sourceSize.height
                                                          / sourceSize.width))
                    }
                }
            }

            // 页码提示
            Rectangle {
                anchors.right: pageImg.right
                anchors.bottom: pageImg.bottom
                anchors.margins: 4
                width: pageLabel.width + 8
                height: pageLabel.height + 4
                radius: 3
                color: "#88000000"
                visible: pageImg.status === Image.Ready

                Label {
                    id: pageLabel
                    anchors.centerIn: parent
                    text: (index + 1) + " / " + (webtoon.controller
                                                 ? webtoon.controller.pageCount : 0)
                    color: "white"
                    font.pixelSize: 11
                }
            }
        }

        // 翻到边界时通知控制器，便于显示"已到末尾"
        onContentYChanged: {
            if (webtoon.controller && webtoon.controller.pageCount > 0) {
                const visible = webtoon.currentVisiblePage()
                if (visible !== webtoon.controller.currentPage)
                    webtoon.controller.goToPage(visible)
            }
        }
    }

    // 底部页码指示器
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        width: indicator.implicitWidth + 20
        height: 28
        radius: 14
        color: "#cc1e1e1e"
        visible: webtoon.controller && webtoon.controller.pageCount > 0

        Label {
            id: indicator
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 12
            text: webtoon.controller
                  ? qsTr("Page") + " " + (webtoon.controller.currentPage + 1)
                    + " / " + webtoon.controller.pageCount
                  : ""
        }
    }

    // 键盘：左右/上下整页跳转
    Keys.onPressed: function (event) {
        if (!webtoon.controller || webtoon.controller.pageCount <= 0) {
            event.accepted = false
            return
        }
        const cur = webtoon.controller.currentPage
        if (event.key === Qt.Key_Right || event.key === Qt.Key_Down) {
            webtoon.scrollToPage(cur + webtoon.pageStride)
            event.accepted = true
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up) {
            webtoon.scrollToPage(cur - webtoon.pageStride)
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
    // 焦点默认会被工具栏按钮抢走，导致键盘翻页失效，故显式夺取焦点
    focus: true
    Component.onCompleted: forceActiveFocus()
}
