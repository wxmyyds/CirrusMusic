find_package(Qt6 REQUIRED COMPONENTS Test Sql)
# For tests that include openssl_primitives.h (cirrus_netease_protocol links it privately).
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)

qt_add_executable(tst_netease_protocol
    unit/netease_protocol_test.cpp
)
target_link_libraries(tst_netease_protocol PRIVATE cirrus_netease_protocol Qt6::Test)
add_test(NAME netease_protocol COMMAND tst_netease_protocol)
set_tests_properties(netease_protocol PROPERTIES TIMEOUT 15)

qt_add_executable(tst_netease_crypto unit/netease_crypto_test.cpp)
target_link_libraries(tst_netease_crypto PRIVATE cirrus_netease_protocol Qt6::Test)
add_test(NAME netease_crypto COMMAND tst_netease_crypto)
set_tests_properties(netease_crypto PROPERTIES TIMEOUT 15)

qt_add_executable(tst_http_transport unit/http_transport_test.cpp)
target_link_libraries(tst_http_transport PRIVATE cirrus_netease_network Qt6::Test)
add_test(NAME http_transport COMMAND tst_http_transport)
set_tests_properties(http_transport PROPERTIES TIMEOUT 15)

qt_add_executable(tst_response_decoder unit/response_decoder_test.cpp)
target_link_libraries(tst_response_decoder PRIVATE cirrus_netease_protocol Qt6::Test)
add_test(NAME response_decoder COMMAND tst_response_decoder)
set_tests_properties(response_decoder PROPERTIES TIMEOUT 15)

# Application test deliberately links no Network, OpenSSL or infrastructure target.
qt_add_executable(tst_session_coordinator unit/session_coordinator_test.cpp)
target_link_libraries(tst_session_coordinator PRIVATE cirrus_application Qt6::Test)
add_test(NAME session_coordinator COMMAND tst_session_coordinator)
set_tests_properties(session_coordinator PROPERTIES TIMEOUT 15)

qt_add_executable(tst_session_cookie_store unit/session_cookie_store_test.cpp)
target_link_libraries(tst_session_cookie_store PRIVATE cirrus_application cirrus_netease_session Qt6::Test)
add_test(NAME session_cookie_store COMMAND tst_session_cookie_store)
set_tests_properties(session_cookie_store PROPERTIES TIMEOUT 15)

qt_add_executable(tst_session_api_client unit/session_api_client_test.cpp)
target_link_libraries(tst_session_api_client PRIVATE cirrus_netease_api Qt6::Test)
add_test(NAME session_api_client COMMAND tst_session_api_client)
set_tests_properties(session_api_client PROPERTIES TIMEOUT 15)

qt_add_executable(tst_xeapi unit/xeapi_test.cpp)
target_link_libraries(tst_xeapi PRIVATE cirrus_netease_api Qt6::Test)
add_test(NAME xeapi COMMAND tst_xeapi)
set_tests_properties(xeapi PROPERTIES TIMEOUT 15)

qt_add_executable(tst_storage unit/storage_test.cpp)
target_link_libraries(tst_storage PRIVATE cirrus_storage Qt6::Test)
add_test(NAME storage COMMAND tst_storage)
set_tests_properties(storage PROPERTIES TIMEOUT 15)

qt_add_executable(tst_listening_stats unit/listening_stats_test.cpp)
target_link_libraries(tst_listening_stats PRIVATE cirrus_listening_state Qt6::Test)
add_test(NAME listening_stats COMMAND tst_listening_stats)
set_tests_properties(listening_stats PROPERTIES TIMEOUT 30)

qt_add_executable(tst_secrets unit/secrets_test.cpp)
target_link_libraries(tst_secrets PRIVATE cirrus_bootstrap Qt6::Test)
add_test(NAME secrets COMMAND tst_secrets)
set_tests_properties(secrets PROPERTIES TIMEOUT 30)

qt_add_executable(tst_bootstrap unit/bootstrap_test.cpp)
target_link_libraries(tst_bootstrap PRIVATE cirrus_bootstrap Qt6::Test)
add_test(NAME bootstrap COMMAND tst_bootstrap)
set_tests_properties(bootstrap PROPERTIES TIMEOUT 15)

qt_add_executable(tst_qr_login_gateway unit/qr_login_gateway_test.cpp)
target_link_libraries(tst_qr_login_gateway PRIVATE cirrus_netease_auth Qt6::Test)
add_test(NAME qr_login_gateway COMMAND tst_qr_login_gateway)
set_tests_properties(qr_login_gateway PROPERTIES TIMEOUT 15)

qt_add_executable(tst_qr_login_service unit/qr_login_service_test.cpp)
target_link_libraries(tst_qr_login_service PRIVATE cirrus_application Qt6::Test)
add_test(NAME qr_login_service COMMAND tst_qr_login_service)
set_tests_properties(qr_login_service PROPERTIES TIMEOUT 15)

qt_add_executable(tst_auth_transaction unit/auth_transaction_test.cpp)
target_link_libraries(tst_auth_transaction PRIVATE cirrus_netease_auth Qt6::Test)
add_test(NAME auth_transaction COMMAND tst_auth_transaction)
set_tests_properties(auth_transaction PROPERTIES TIMEOUT 15)

qt_add_executable(tst_account_restore unit/account_restore_test.cpp)
target_link_libraries(tst_account_restore PRIVATE cirrus_netease_auth Qt6::Test)
add_test(NAME account_restore COMMAND tst_account_restore)
set_tests_properties(account_restore PROPERTIES TIMEOUT 20)

qt_add_executable(tst_backend_facade unit/backend_facade_test.cpp)
target_link_libraries(tst_backend_facade PRIVATE cirrus_application Qt6::Test)
add_test(NAME backend_facade COMMAND tst_backend_facade)
set_tests_properties(backend_facade PROPERTIES TIMEOUT 15)

qt_add_executable(tst_backend_runtime unit/backend_runtime_test.cpp)
target_link_libraries(tst_backend_runtime PRIVATE cirrus_backend cirrus_bootstrap Qt6::Test Qt6::Gui OpenSSL::Crypto)
add_test(NAME backend_runtime COMMAND tst_backend_runtime)
# Some 40 cases, one waiting out the facade's 5 s close delay: 22 s in a Linux debug build,
# past 30 s on the macOS runner (three cores, four tests at a time).
set_tests_properties(backend_runtime PROPERTIES TIMEOUT 90)

qt_add_executable(tst_music unit/music_test.cpp)
target_link_libraries(tst_music PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME music COMMAND tst_music)
set_tests_properties(music PROPERTIES TIMEOUT 15)

qt_add_executable(tst_stream unit/stream_test.cpp)
target_link_libraries(tst_stream PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME stream COMMAND tst_stream)
set_tests_properties(stream PROPERTIES TIMEOUT 15)

# Keep the coordinator independently linkable using Core/Test only.
qt_add_executable(tst_playback unit/playback_test.cpp)
target_link_libraries(tst_playback PRIVATE cirrus_application Qt6::Test)
add_test(NAME playback COMMAND tst_playback)
set_tests_properties(playback PROPERTIES TIMEOUT 15)

# Offline identity/media seam, with no Network/Multimedia in this test target.
qt_add_executable(tst_media_seam unit/media_seam_test.cpp)
target_link_libraries(tst_media_seam PRIVATE cirrus_application Qt6::Test)
add_test(NAME media_seam COMMAND tst_media_seam)
set_tests_properties(media_seam PROPERTIES TIMEOUT 15)

qt_add_executable(tst_media unit/media_test.cpp)
target_link_libraries(tst_media PRIVATE cirrus_media Qt6::Test)
add_test(NAME media COMMAND tst_media)
set_tests_properties(media PROPERTIES TIMEOUT 30)

qt_add_executable(tst_audio_store unit/audio_store_test.cpp)
target_link_libraries(tst_audio_store PRIVATE cirrus_media Qt6::Test)
add_test(NAME audio_store COMMAND tst_audio_store)
set_tests_properties(audio_store PROPERTIES TIMEOUT 60)

qt_add_executable(tst_audio_decoder unit/audio_decoder_test.cpp)
target_link_libraries(tst_audio_decoder PRIVATE cirrus_media Qt6::Test)
target_compile_definitions(tst_audio_decoder PRIVATE AUDIO_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audio")
add_test(NAME audio_decoder COMMAND tst_audio_decoder)
set_tests_properties(audio_decoder PROPERTIES TIMEOUT 60)

# Plays on the default output at volume 0; skips itself without a device.
# Transitions between tracks: plans, gains, decks (docs/tasks/automix.md); no device.
qt_add_executable(tst_automix unit/automix_test.cpp)
target_link_libraries(tst_automix PRIVATE cirrus_media Qt6::Test)
target_compile_definitions(tst_automix PRIVATE AUDIO_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audio")
add_test(NAME automix COMMAND tst_automix)
set_tests_properties(automix PROPERTIES TIMEOUT 30)

qt_add_executable(tst_audio_engine unit/audio_engine_test.cpp)
target_link_libraries(tst_audio_engine PRIVATE cirrus_media Qt6::Test)
target_compile_definitions(tst_audio_engine PRIVATE AUDIO_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audio")
add_test(NAME audio_engine COMMAND tst_audio_engine)
set_tests_properties(audio_engine PROPERTIES TIMEOUT 120)

qt_add_executable(tst_listening unit/listening_test.cpp)
target_link_libraries(tst_listening PRIVATE cirrus_application Qt6::Test)
add_test(NAME listening COMMAND tst_listening)
set_tests_properties(listening PROPERTIES TIMEOUT 15)

# Core/Test only: no platform, JSON/Network or audio dependencies.
qt_add_executable(tst_playback_observation unit/playback_observation_test.cpp)
target_link_libraries(tst_playback_observation PRIVATE cirrus_application Qt6::Test)
add_test(NAME playback_observation COMMAND tst_playback_observation)
set_tests_properties(playback_observation PROPERTIES TIMEOUT 30)

qt_add_executable(tst_playback_observation_codec unit/playback_observation_codec_test.cpp)
target_link_libraries(tst_playback_observation_codec PRIVATE cirrus_observation_codec cirrus_application Qt6::Test)
target_compile_definitions(tst_playback_observation_codec PRIVATE OBSERVATION_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME playback_observation_codec COMMAND tst_playback_observation_codec)
set_tests_properties(playback_observation_codec PROPERTIES TIMEOUT 30)

# Portable protocol + fake sink only, no application/player/Network linkage.
qt_add_executable(tst_playback_observation_connection unit/playback_observation_connection_test.cpp)
target_link_libraries(tst_playback_observation_connection PRIVATE cirrus_observation_codec Qt6::Test)
add_test(NAME playback_observation_connection COMMAND tst_playback_observation_connection)
set_tests_properties(playback_observation_connection PROPERTIES TIMEOUT 30)

# Owner private barriers + scalar wake composition; only Core/Test, no native I/O.
qt_add_executable(tst_playback_observation_feed unit/playback_observation_feed_test.cpp)
target_link_libraries(tst_playback_observation_feed PRIVATE cirrus_observation_feed Qt6::Test)
add_test(NAME playback_observation_feed COMMAND tst_playback_observation_feed)
set_tests_properties(playback_observation_feed PROPERTIES TIMEOUT 30)

if(TARGET cirrus_observation_linux)
    qt_add_executable(tst_linux_observation_endpoint unit/linux_observation_endpoint_test.cpp)
    target_link_libraries(tst_linux_observation_endpoint PRIVATE cirrus_observation_linux Qt6::Test)
    add_test(NAME linux_observation_endpoint COMMAND tst_linux_observation_endpoint)
    set_tests_properties(linux_observation_endpoint PROPERTIES TIMEOUT 15)
    qt_add_executable(tst_linux_observation_io unit/linux_observation_io_test.cpp)
    target_link_libraries(tst_linux_observation_io PRIVATE cirrus_observation_linux_service Qt6::Test)
    add_test(NAME linux_observation_io COMMAND tst_linux_observation_io)
    set_tests_properties(linux_observation_io PROPERTIES TIMEOUT 40)
    qt_add_executable(tst_linux_observation_service unit/linux_observation_service_test.cpp)
    target_link_libraries(tst_linux_observation_service PRIVATE cirrus_observation_linux_service cirrus_observation_linux_client Qt6::Test)
    add_test(NAME linux_observation_service COMMAND tst_linux_observation_service)
    set_tests_properties(linux_observation_service PROPERTIES TIMEOUT 30)
    qt_add_executable(tst_linux_observation_client unit/linux_observation_client_test.cpp)
    target_link_libraries(tst_linux_observation_client PRIVATE cirrus_observation_linux_client Qt6::Test)
    add_test(NAME linux_observation_client COMMAND tst_linux_observation_client)
    set_tests_properties(linux_observation_client PROPERTIES TIMEOUT 30)
endif()

qt_add_executable(tst_catalog unit/catalog_test.cpp)
target_link_libraries(tst_catalog PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME catalog COMMAND tst_catalog)
set_tests_properties(catalog PROPERTIES TIMEOUT 15)

qt_add_executable(tst_catalog_metadata unit/catalog_metadata_test.cpp)
target_link_libraries(tst_catalog_metadata PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME catalog_metadata COMMAND tst_catalog_metadata)
set_tests_properties(catalog_metadata PROPERTIES TIMEOUT 30)

qt_add_executable(tst_library unit/library_test.cpp)
target_link_libraries(tst_library PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME library COMMAND tst_library)
set_tests_properties(library PROPERTIES TIMEOUT 20)

qt_add_executable(tst_discovery unit/discovery_test.cpp)
target_link_libraries(tst_discovery PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME discovery COMMAND tst_discovery)
set_tests_properties(discovery PROPERTIES TIMEOUT 15)

# Dynamic FM supply stays Core-only, independently of provider/network/media.
qt_add_executable(tst_personal_fm unit/personal_fm_test.cpp)
target_link_libraries(tst_personal_fm PRIVATE cirrus_application Qt6::Test)
add_test(NAME personal_fm COMMAND tst_personal_fm)
set_tests_properties(personal_fm PROPERTIES TIMEOUT 15)

qt_add_executable(tst_fm_listening unit/fm_listening_test.cpp)
target_link_libraries(tst_fm_listening PRIVATE cirrus_application Qt6::Test)
add_test(NAME fm_listening COMMAND tst_fm_listening)
set_tests_properties(fm_listening PROPERTIES TIMEOUT 20)

qt_add_executable(tst_listening_history unit/listening_history_test.cpp)
target_link_libraries(tst_listening_history PRIVATE cirrus_application Qt6::Test)
add_test(NAME listening_history COMMAND tst_listening_history)
set_tests_properties(listening_history PROPERTIES TIMEOUT 20)

qt_add_executable(tst_listening_reporter unit/listening_reporter_test.cpp)
target_link_libraries(tst_listening_reporter PRIVATE cirrus_application Qt6::Test)
add_test(NAME listening_reporter COMMAND tst_listening_reporter)
set_tests_properties(listening_reporter PROPERTIES TIMEOUT 15)

qt_add_executable(tst_together_session unit/together_session_test.cpp)
target_link_libraries(tst_together_session PRIVATE cirrus_application Qt6::Test)
add_test(NAME together_session COMMAND tst_together_session)
set_tests_properties(together_session PROPERTIES TIMEOUT 30)

qt_add_executable(tst_client_log unit/client_log_test.cpp)
target_link_libraries(tst_client_log PRIVATE cirrus_netease_protocol Qt6::Test)
add_test(NAME client_log COMMAND tst_client_log)
set_tests_properties(client_log PROPERTIES TIMEOUT 15)

qt_add_executable(tst_listening_report unit/listening_report_test.cpp)
target_link_libraries(tst_listening_report PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME listening_report COMMAND tst_listening_report)
set_tests_properties(listening_report PROPERTIES TIMEOUT 20)

qt_add_executable(tst_collections unit/collections_test.cpp)
target_link_libraries(tst_collections PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME collections COMMAND tst_collections)
set_tests_properties(collections PROPERTIES TIMEOUT 60)

qt_add_executable(tst_playlist_writes unit/playlist_writes_test.cpp)
target_link_libraries(tst_playlist_writes PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME playlist_writes COMMAND tst_playlist_writes)
set_tests_properties(playlist_writes PROPERTIES TIMEOUT 40)

qt_add_executable(tst_playlist_cover_writes unit/playlist_cover_writes_test.cpp support/synthetic_cover.h)
target_link_libraries(tst_playlist_cover_writes PRIVATE cirrus_netease_music Qt6::Test Qt6::Gui)
add_test(NAME playlist_cover_writes COMMAND tst_playlist_cover_writes)
set_tests_properties(playlist_cover_writes PROPERTIES TIMEOUT 40)

qt_add_executable(tst_playlist_tag_writes unit/playlist_tag_writes_test.cpp)
target_link_libraries(tst_playlist_tag_writes PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME playlist_tag_writes COMMAND tst_playlist_tag_writes)
set_tests_properties(playlist_tag_writes PROPERTIES TIMEOUT 30)

qt_add_executable(tst_playlist_order unit/playlist_order_test.cpp)
target_link_libraries(tst_playlist_order PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME playlist_order COMMAND tst_playlist_order)
set_tests_properties(playlist_order PROPERTIES TIMEOUT 30)

qt_add_executable(tst_account_library unit/account_library_test.cpp)
target_link_libraries(tst_account_library PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME account_library COMMAND tst_account_library)
set_tests_properties(account_library PROPERTIES TIMEOUT 30)

qt_add_executable(tst_search_assist unit/search_assist_test.cpp support/synthetic_search_assist.h)
target_link_libraries(tst_search_assist PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME search_assist COMMAND tst_search_assist)
set_tests_properties(search_assist PROPERTIES TIMEOUT 30)

qt_add_executable(tst_comment_writes unit/comment_writes_test.cpp support/comment_write_support.h)
target_link_libraries(tst_comment_writes PRIVATE cirrus_netease_music Qt6::Test OpenSSL::Crypto)
add_test(NAME comment_writes COMMAND tst_comment_writes)
set_tests_properties(comment_writes PROPERTIES TIMEOUT 45)

qt_add_executable(tst_comments unit/comments_test.cpp)
target_link_libraries(tst_comments PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME comments COMMAND tst_comments)
set_tests_properties(comments PROPERTIES TIMEOUT 40)

qt_add_executable(tst_podcast_reads unit/podcast_reads_test.cpp)
target_link_libraries(tst_podcast_reads PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME podcast_reads COMMAND tst_podcast_reads)
set_tests_properties(podcast_reads PROPERTIES TIMEOUT 30)

qt_add_executable(tst_entity_search unit/entity_search_test.cpp)
target_link_libraries(tst_entity_search PRIVATE cirrus_netease_music Qt6::Test OpenSSL::Crypto)
add_test(NAME entity_search COMMAND tst_entity_search)
set_tests_properties(entity_search PROPERTIES TIMEOUT 25)

qt_add_executable(tst_together_http unit/together_http_test.cpp support/synthetic_together_http.h)
target_link_libraries(tst_together_http PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME together_http COMMAND tst_together_http)
set_tests_properties(together_http PROPERTIES TIMEOUT 60)

qt_add_executable(tst_recommendation_history unit/recommendation_history_test.cpp
    support/synthetic_recommendation_history.h)
target_link_libraries(tst_recommendation_history PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME recommendation_history COMMAND tst_recommendation_history)
set_tests_properties(recommendation_history PROPERTIES TIMEOUT 30)

qt_add_executable(tst_daily_playlists unit/daily_playlists_test.cpp support/synthetic_daily_playlists.h)
target_link_libraries(tst_daily_playlists PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME daily_playlists COMMAND tst_daily_playlists)
set_tests_properties(daily_playlists PROPERTIES TIMEOUT 25)

qt_add_executable(tst_discovery_browse unit/discovery_browse_test.cpp support/synthetic_discovery_browse.h)
target_link_libraries(tst_discovery_browse PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME discovery_browse COMMAND tst_discovery_browse)
set_tests_properties(discovery_browse PROPERTIES TIMEOUT 60)

qt_add_executable(tst_song_wiki_text unit/song_wiki_text_test.cpp support/synthetic_song_wiki_text.h)
target_link_libraries(tst_song_wiki_text PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME song_wiki_text COMMAND tst_song_wiki_text)
set_tests_properties(song_wiki_text PROPERTIES TIMEOUT 60)

qt_add_executable(tst_playlist_discovery unit/playlist_discovery_test.cpp)
target_link_libraries(tst_playlist_discovery PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME playlist_discovery COMMAND tst_playlist_discovery)
set_tests_properties(playlist_discovery PROPERTIES TIMEOUT 25)

qt_add_executable(tst_charts unit/charts_test.cpp)
target_link_libraries(tst_charts PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME charts COMMAND tst_charts)
set_tests_properties(charts PROPERTIES TIMEOUT 20)

qt_add_executable(tst_cloud_history unit/cloud_history_test.cpp)
target_link_libraries(tst_cloud_history PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME cloud_history COMMAND tst_cloud_history)
set_tests_properties(cloud_history PROPERTIES TIMEOUT 20)

qt_add_executable(tst_logging unit/logging_test.cpp)
target_link_libraries(tst_logging PRIVATE cirrus_logging Qt6::Test)
add_test(NAME logging COMMAND tst_logging)
set_tests_properties(logging PROPERTIES TIMEOUT 30)

qt_add_executable(tst_log_canary unit/log_canary_test.cpp support/fake_http.h support/fake_range_http.h)
target_link_libraries(tst_log_canary PRIVATE cirrus_netease_api cirrus_media cirrus_application cirrus_bilibili cirrus_secrets Qt6::Test)
add_test(NAME log_canary COMMAND tst_log_canary)
set_tests_properties(log_canary PROPERTIES TIMEOUT 30)

qt_add_executable(tst_cover_checks unit/cover_checks_test.cpp)
target_link_libraries(tst_cover_checks PRIVATE cirrus_resource_cache Qt6::Test Qt6::Gui)
add_test(NAME cover_checks COMMAND tst_cover_checks)

# The cover palette's black-and-white rule (a plain C++ check of ui/theme, not a QML test).
qt_add_executable(tst_cover_palette unit/cover_palette_test.cpp)
target_include_directories(tst_cover_palette PRIVATE ${PROJECT_SOURCE_DIR}/ui/theme)
target_link_libraries(tst_cover_palette PRIVATE cirrus_ui_theme cirrus_mcu Qt6::Test Qt6::Gui)
add_test(NAME cover_palette COMMAND tst_cover_palette)

qt_add_executable(tst_resource_cache_store unit/resource_cache_store_test.cpp)
target_link_libraries(tst_resource_cache_store PRIVATE cirrus_resource_cache Qt6::Test)
add_test(NAME resource_cache_store COMMAND tst_resource_cache_store)
set_tests_properties(resource_cache_store PROPERTIES TIMEOUT 30)

qt_add_executable(tst_user_profile unit/user_profile_test.cpp support/synthetic_user_profile.h)
target_link_libraries(tst_user_profile PRIVATE cirrus_netease_music Qt6::Test)
add_test(NAME user_profile COMMAND tst_user_profile)

# The update check's pure parts: versions, the release answer, the notes (no network).
qt_add_executable(tst_update_release unit/update_release_test.cpp
    ${PROJECT_SOURCE_DIR}/ui/presentation/update_release.cpp)
target_include_directories(tst_update_release PRIVATE ${PROJECT_SOURCE_DIR}/ui/presentation)
target_link_libraries(tst_update_release PRIVATE Qt6::Test Qt6::Core)
add_test(NAME update_release COMMAND tst_update_release)

qt_add_executable(tst_bili_lyrics unit/bili_lyrics_test.cpp support/synthetic_bilibili.h support/fake_http.h)
target_link_libraries(tst_bili_lyrics PRIVATE cirrus_bilibili Qt6::Test)
add_test(NAME bili_lyrics COMMAND tst_bili_lyrics)
set_tests_properties(bili_lyrics PROPERTIES TIMEOUT 20)

qt_add_executable(tst_bili_account unit/bili_account_test.cpp support/synthetic_bilibili.h support/fake_http.h)
target_link_libraries(tst_bili_account PRIVATE cirrus_bilibili cirrus_secrets Qt6::Test)
add_test(NAME bili_account COMMAND tst_bili_account)
set_tests_properties(bili_account PROPERTIES TIMEOUT 20)

# Bilibili's pure pieces: WBI signature, matcher score, stream selection, playurl mapping.
qt_add_executable(tst_bili_wbi unit/bili_wbi_test.cpp)
target_link_libraries(tst_bili_wbi PRIVATE cirrus_bilibili Qt6::Test OpenSSL::Crypto)
add_test(NAME bili_wbi COMMAND tst_bili_wbi)
set_tests_properties(bili_wbi PROPERTIES TIMEOUT 20)

qt_add_executable(tst_bili_matcher unit/bili_matcher_test.cpp)
target_link_libraries(tst_bili_matcher PRIVATE cirrus_bilibili Qt6::Test)
add_test(NAME bili_matcher COMMAND tst_bili_matcher)
set_tests_properties(bili_matcher PROPERTIES TIMEOUT 20)

qt_add_executable(tst_bili_quality unit/bili_quality_test.cpp support/bili_playurl_fixtures.h)
target_link_libraries(tst_bili_quality PRIVATE cirrus_bilibili Qt6::Test)
add_test(NAME bili_quality COMMAND tst_bili_quality)
set_tests_properties(bili_quality PROPERTIES TIMEOUT 20)

# The router's decision table (fake resolvers, no network).
qt_add_executable(tst_provider_router unit/provider_router_test.cpp)
target_link_libraries(tst_provider_router PRIVATE cirrus_application Qt6::Test)
add_test(NAME provider_router COMMAND tst_provider_router)
set_tests_properties(provider_router PROPERTIES TIMEOUT 20)

# The Bilibili API chain over a synthetic QNetworkAccessManager: fingerprint -> nav -> search
# -> view -> playurl -> size probe, and each typed failure.
qt_add_executable(tst_bili_api unit/bili_api_test.cpp support/fake_http.h support/synthetic_bilibili.h)
target_link_libraries(tst_bili_api PRIVATE cirrus_bilibili Qt6::Test Qt6::Network)
add_test(NAME bili_api COMMAND tst_bili_api)
set_tests_properties(bili_api PROPERTIES TIMEOUT 20)

qt_add_executable(tst_bili_http unit/bili_http_test.cpp support/fake_http.h)
target_link_libraries(tst_bili_http PRIVATE cirrus_bilibili Qt6::Test)
add_test(NAME bili_http COMMAND tst_bili_http)
set_tests_properties(bili_http PROPERTIES TIMEOUT 20)

qt_add_executable(tst_bili_resolver unit/bili_resolver_test.cpp support/synthetic_bilibili.h)
target_link_libraries(tst_bili_resolver PRIVATE cirrus_bilibili Qt6::Test)
add_test(NAME bili_resolver COMMAND tst_bili_resolver)
set_tests_properties(bili_resolver PROPERTIES TIMEOUT 20)
# Romaji over Japanese lyrics (docs/ui/pages/player.md §6.3), on its own (no QML).
qt_add_executable(tst_ruby unit/ruby_test.cpp ${PROJECT_SOURCE_DIR}/ui/presentation/ruby.cpp)
target_include_directories(tst_ruby PRIVATE ${PROJECT_SOURCE_DIR}/ui/presentation)
target_compile_definitions(tst_ruby PRIVATE
    CIRRUS_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/ui/samples")
target_link_libraries(tst_ruby PRIVATE Qt6::Test Qt6::Core Qt6::Qml)
add_test(NAME ruby COMMAND tst_ruby)

# The share text and links (docs/ui/pages/share.md), on its own.
qt_add_executable(tst_share unit/share_test.cpp ${PROJECT_SOURCE_DIR}/ui/presentation/share_text.cpp)
target_include_directories(tst_share PRIVATE ${PROJECT_SOURCE_DIR}/ui/presentation)
target_link_libraries(tst_share PRIVATE Qt6::Test Qt6::Core)
add_test(NAME share COMMAND tst_share)
