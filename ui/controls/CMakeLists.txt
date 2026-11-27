find_package(Qt6 REQUIRED COMPONENTS ShaderTools)

qt_add_library(cirrus_ui_controls STATIC)

qt_add_qml_module(cirrus_ui_controls
    URI Cirrus.Controls
    VERSION 1.0
    DEPENDENCIES
        QtQuick
        Cirrus.Theme
        M3Shapes
    SOURCES
        md.h
        input_mode.h input_mode.cpp
        wheel_filter.h wheel_filter.cpp
        shadow_owns_material.h shadow_owns_material.cpp
    QML_FILES
        Icon.qml
        StateLayer.qml
        FocusRing.qml
        Shadow.qml
        Elevation.qml
        MorphShape.qml
        Pop.qml
        ShapeRefresh.qml
        ButtonContainer.qml
        Button.qml
        IconButton.qml
        Carousel.qml
        Fab.qml
        ShapeButton.qml
        ButtonGroupLayout.qml
        StandardButtonGroup.qml
        ConnectedButtonGroup.qml
        Switch.qml
        CheckBox.qml
        RadioButton.qml
        ToolTip.qml
        ScrollBar.qml
        SelectableText.qml
        TextField.qml
        TextArea.qml
        SearchBar.qml
        Dialog.qml
        Snackbar.qml
        Slider.qml
        ProgressBar.qml
        WaveLine.qml
        CircularProgress.qml
        LoadingIndicator.qml
        ListItem.qml
        Menu.qml
        MenuItem.qml
        MenuSeparator.qml
        NavigationRail.qml
        NavigationRailItem.qml
        TabBar.qml
        TabButton.qml
        Chip.qml
        ShortcutKey.qml
        ChipGroup.qml
)

# Embedded at :/qt/qml/Cirrus/Controls/shaders/*.qsb, next to the QML files.
qt_add_shaders(cirrus_ui_controls "cirrus_ui_controls_shaders"
    PREFIX "/qt/qml/Cirrus/Controls"
    FILES
        shaders/ripple.frag
        shaders/wave.frag
)

target_link_libraries(cirrus_ui_controls PRIVATE Qt6::Quick cirrus_ui_theme m3shapes)
