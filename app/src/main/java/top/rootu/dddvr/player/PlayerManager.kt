package top.rootu.dddvr.player

import android.annotation.SuppressLint
import android.content.Context
import android.media.MediaFormat
import android.os.Build
import android.media.audiofx.LoudnessEnhancer
import android.os.Handler
import android.util.Log
import android.view.accessibility.CaptioningManager
import androidx.core.net.toUri
import androidx.core.os.LocaleListCompat
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.ColorInfo
import androidx.media3.common.Format
import androidx.media3.common.MediaMetadata
import androidx.media3.common.MimeTypes
import androidx.media3.common.Player
import androidx.media3.common.TrackSelectionOverride
import androidx.media3.common.Tracks
import androidx.media3.common.audio.ChannelMixingAudioProcessor
import androidx.media3.datasource.DefaultDataSource
import androidx.media3.datasource.okhttp.OkHttpDataSource
import androidx.media3.exoplayer.DefaultLoadControl
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.Renderer
import androidx.media3.exoplayer.analytics.AnalyticsListener
import androidx.media3.exoplayer.audio.AudioRendererEventListener
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.AudioTrackAudioOutputProvider
import androidx.media3.exoplayer.audio.DefaultAudioSink
import androidx.media3.exoplayer.hls.DefaultHlsExtractorFactory
import androidx.media3.exoplayer.hls.HlsMediaSource
import androidx.media3.exoplayer.mediacodec.MediaCodecAdapter
import androidx.media3.exoplayer.mediacodec.MediaCodecSelector
import androidx.media3.exoplayer.mediacodec.MediaCodecUtil
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory
import androidx.media3.exoplayer.source.MediaSource
import androidx.media3.exoplayer.trackselection.DefaultTrackSelector
import androidx.media3.exoplayer.upstream.DefaultLoadErrorHandlingPolicy
import androidx.media3.exoplayer.video.MediaCodecVideoRenderer
import androidx.media3.exoplayer.video.VideoRendererEventListener
import androidx.media3.extractor.DefaultExtractorsFactory
import androidx.media3.extractor.mp4.Mp4Extractor
import androidx.media3.extractor.ts.DefaultTsPayloadReaderFactory
import androidx.media3.extractor.ts.TsExtractor
import androidx.media3.session.MediaSession
import okhttp3.OkHttpClient
import top.rootu.dddvr.App.Companion.USER_AGENT
import top.rootu.dddvr.data.SettingsRepository
import top.rootu.dddvr.logic.AudioMixerLogic
import top.rootu.dddvr.logic.TrackLogic
import top.rootu.dddvr.logic.UnifiedMetadataReader
import top.rootu.dddvr.model.MediaItem
import top.rootu.dddvr.utils.MediaFormatHelper
import top.rootu.dddvr.viewmodel.TrackOption
import top.rootu.dddvr.xr.ui.OpenXrTrackRow
import java.nio.ByteBuffer
import java.security.SecureRandom
import java.security.cert.X509Certificate
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager
import androidx.media3.common.MediaItem as Media3MediaItem

private const val MAX_UHD_VIDEO_WIDTH = 8192
private const val MAX_UHD_VIDEO_HEIGHT = 4320
private const val COLOR_FORMAT_YUVP010 = 0x36

private class HdrMetadataVideoRenderer(
    context: Context,
    codecAdapterFactory: MediaCodecAdapter.Factory,
    mediaCodecSelector: MediaCodecSelector,
    allowedJoiningTimeMs: Long,
    enableDecoderFallback: Boolean,
    eventHandler: Handler,
    eventListener: VideoRendererEventListener,
    maxDroppedFramesToNotify: Int
) : MediaCodecVideoRenderer(
    context,
    codecAdapterFactory,
    mediaCodecSelector,
    allowedJoiningTimeMs,
    enableDecoderFallback,
    eventHandler,
    eventListener,
    maxDroppedFramesToNotify
) {
    override fun getMediaFormat(
        format: Format,
        codecMimeType: String,
        codecMaxValues: CodecMaxValues,
        codecOperatingRate: Float,
        deviceNeedsNoPostProcessWorkaround: Boolean,
        tunnelingAudioSessionId: Int
    ): MediaFormat {
        val mediaFormat = super.getMediaFormat(
            format,
            codecMimeType,
            codecMaxValues,
            codecOperatingRate,
            deviceNeedsNoPostProcessWorkaround,
            tunnelingAudioSessionId
        )
        val colorInfo = format.colorInfo
        val codecs = format.codecs.orEmpty()
        val isDolbyVision = format.sampleMimeType == MimeTypes.VIDEO_DOLBY_VISION ||
            codecs.startsWith("dvh1", ignoreCase = true) ||
            codecs.startsWith("dvhe", ignoreCase = true)
        val isHdrTransfer = colorInfo?.let(ColorInfo::isTransferHdr) == true
        if (!isHdrTransfer && !isDolbyVision) {
            return mediaFormat
        }

        val colorStandard = colorInfo?.colorSpace
            ?.takeIf { it != Format.NO_VALUE && it > 0 }
            ?: if (isDolbyVision) MediaFormat.COLOR_STANDARD_BT2020 else null
        val colorTransfer = colorInfo?.colorTransfer
            ?.takeIf { it != Format.NO_VALUE && it > 0 }
            ?: if (isDolbyVision) MediaFormat.COLOR_TRANSFER_ST2084 else null
        val colorRange = colorInfo?.colorRange
            ?.takeIf { it != Format.NO_VALUE && it > 0 }
            ?: if (isDolbyVision) MediaFormat.COLOR_RANGE_LIMITED else null

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            colorStandard?.let { mediaFormat.setInteger(MediaFormat.KEY_COLOR_STANDARD, it) }
            colorTransfer?.let { mediaFormat.setInteger(MediaFormat.KEY_COLOR_TRANSFER, it) }
            colorRange?.let { mediaFormat.setInteger(MediaFormat.KEY_COLOR_RANGE, it) }
            colorInfo?.hdrStaticInfo?.takeIf { it.isNotEmpty() }?.let { staticInfo ->
                mediaFormat.setByteBuffer("hdr-static-info", ByteBuffer.wrap(staticInfo))
            }
        }

        val p010Supported = runCatching {
            MediaCodecUtil.getDecoderInfos(codecMimeType, false, false).any { decoderInfo ->
                decoderInfo.capabilities?.colorFormats?.any { it == COLOR_FORMAT_YUVP010 } == true
            }
        }.getOrDefault(false)
        Log.i(
            "DDDVR/PlayerManager",
                "VIDEO_HDR_METADATA_APPLIED width=${format.width} height=${format.height} " +
                "mime=${format.sampleMimeType} codecMime=$codecMimeType codecs=$codecs " +
                "standard=${colorStandard ?: -1} transfer=${colorTransfer ?: -1} " +
                "range=${colorRange ?: -1} sourceStandard=${colorInfo?.colorSpace ?: -1} " +
                "sourceTransfer=${colorInfo?.colorTransfer ?: -1} sourceRange=${colorInfo?.colorRange ?: -1} " +
                "hdrStatic=${colorInfo?.hdrStaticInfo?.size ?: 0} " +
                "dolbyVision=$isDolbyVision p010Supported=$p010Supported"
        )
        return mediaFormat
    }
}

class PlayerManager(
    private val context: Context,
    listener: Player.Listener
) {
    private var playerListener: Player.Listener? = listener
    private val appContext = context.applicationContext
    private val settingsRepo = SettingsRepository.getInstance(appContext)

    var exoPlayer: ExoPlayer? = null
        private set

    private var mediaSession: MediaSession? = null
    private var loudnessEnhancer: LoudnessEnhancer? = null

    // Состояние для восстановления
    private var currentWindowIndex = 0
    private var currentPosition = 0L
    private var currentMediaItems: List<Media3MediaItem> = emptyList()
    private var playWhenReady = true
    private var videoTrackDisabled = false

    private var currentTrackInfo: Map<Int, UnifiedMetadataReader.TrackInfo> = emptyMap()
    var onMetadataAvailable: (() -> Unit)? = null
    var onPlayerCreated: ((ExoPlayer) -> Unit)? = null
    var onVideoFormatChanged: ((Format) -> Unit)? = null
    var onAudioOutputFormatChanged: ((String) -> Unit)? = null

    private val resolvedMediaTypes = ConcurrentHashMap<String, String>()

    private val trustAllCerts = arrayOf<TrustManager>(@SuppressLint("CustomX509TrustManager")
    object : X509TrustManager {
        @SuppressLint("TrustAllX509TrustManager")
        override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) {}
        @SuppressLint("TrustAllX509TrustManager")
        override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) {}
        override fun getAcceptedIssuers(): Array<X509Certificate> = arrayOf()
    })

    private val sslContext = SSLContext.getInstance("TLS")
    private fun socketFactory(): SSLSocketFactory {
        sslContext.init(null, trustAllCerts, SecureRandom())
        return sslContext.socketFactory
    }

    private val okHttpClient = OkHttpClient.Builder()
        .connectTimeout(60, TimeUnit.SECONDS)
        .readTimeout(120, TimeUnit.SECONDS)
        .writeTimeout(60, TimeUnit.SECONDS)
        .followRedirects(true)
        .followSslRedirects(true)
        .retryOnConnectionFailure(true)
        // Разрешаем самоподписанные сертификаты (для пользовательских серверов с видео)
        .sslSocketFactory(socketFactory(), trustAllCerts[0] as X509TrustManager)
        .hostnameVerifier { _, _ -> true }
        // Запишим правильный MimeType от сервера, чтобы перезапустить видео при ошибке контейнера
        // Используем Application Interceptor, чтобы поймать оригинальный URL
        .addInterceptor { chain ->
            val request = chain.request()
            val originalUrl = request.url.toString()

            // Выполняем запрос (включая все редиректы)
            val response = chain.proceed(request)

            // Получаем заголовки и финальный URL (после редиректов)
            val contentType = response.header("Content-Type")
            val finalUrl = response.request.url.toString()

            var exoMimeType: String? = null

            // Пытаемся определить по Content-Type
            if (contentType != null) {
                val lowerType = contentType.lowercase()
                exoMimeType = when (lowerType) {
                    "application/x-mpegurl",
                    "application/vnd.apple.mpegurl" -> MimeTypes.APPLICATION_M3U8
                    "application/dash+xml" -> MimeTypes.APPLICATION_MPD
                    "application/vnd.ms-sstr+xml" -> MimeTypes.APPLICATION_SS
                    else -> null
                }
            }

            // Если Content-Type кривой (например octet-stream), смотрим на расширение финального URL
            if (exoMimeType == null) {
                val extension = MediaFormatHelper.getFileExtension(finalUrl.toUri().path ?: "")
                exoMimeType = when (extension) {
                    "m3u8" -> MimeTypes.APPLICATION_M3U8
                    "mpd" -> MimeTypes.APPLICATION_MPD
                    "ism", "isml" -> MimeTypes.APPLICATION_SS
                    else -> null
                }
            }

            // Сохраняем найденный тип
            if (exoMimeType != null) {
                resolvedMediaTypes[originalUrl] = exoMimeType
            }

            response
        }
        .build()

    // Фабрика для HTTP (сеть)
    private val baseHttpFactory = OkHttpDataSource.Factory(okHttpClient)
        .setUserAgent(USER_AGENT)

    // Универсальная фабрика, которая умеет работать с content://, file:// и http://
    // Мы передаем baseHttpFactory как источник для сетевых запросов.
    private val defaultDataSourceFactory = DefaultDataSource.Factory(appContext, baseHttpFactory)

    // Пересоздаем фабрику при инициализации, чтобы гарантировать чистое состояние
    private fun createParsingDataSourceFactory(): ParsingDataSourceFactory {
        return ParsingDataSourceFactory(
            upstreamFactory = defaultDataSourceFactory,
            onMetadataParsed = { metadataMap ->
                currentTrackInfo = metadataMap
                onMetadataAvailable?.invoke()
            },
            isMetadataParsed = { currentTrackInfo.isNotEmpty() }
        )
    }

    private val tsExtractorFlags = DefaultTsPayloadReaderFactory.FLAG_ENABLE_HDMV_DTS_AUDIO_STREAMS or
            DefaultTsPayloadReaderFactory.FLAG_ALLOW_NON_IDR_KEYFRAMES or
            DefaultTsPayloadReaderFactory.FLAG_DETECT_ACCESS_UNITS or
            DefaultTsPayloadReaderFactory.FLAG_IGNORE_SPLICE_INFO_STREAM

    private val extractorsFactory = DefaultExtractorsFactory()
        .setTsExtractorFlags(tsExtractorFlags)
        .setTsExtractorTimestampSearchBytes(5000 * TsExtractor.TS_PACKET_SIZE)
        .setMp4ExtractorFlags(
            Mp4Extractor.FLAG_READ_WITHIN_GOP_SAMPLE_DEPENDENCIES or
                    Mp4Extractor.FLAG_READ_WITHIN_GOP_SAMPLE_DEPENDENCIES_H265
        )
        // Включаем поиск метаданных в начале каждого чанка для MKV
        .setMatroskaExtractorFlags(0)
        .setConstantBitrateSeekingEnabled(true)

    private val loadErrorHandlingPolicy = object : DefaultLoadErrorHandlingPolicy() {
        override fun getMinimumLoadableRetryCount(dataType: Int): Int = 5
    }

    fun initializePlayer() {
        if (exoPlayer != null) {
            releasePlayer(isFinalRelease = false, saveState = true)
        }

        // === Кастомный MediaCodecSelector для Dolby Vision Fallback ===
        val mediaCodecSelector = if (settingsRepo.isMapDvToHevcEnabled()) {
            MediaCodecSelector { mimeType, requiresSecureDecoder, requiresTunnelingDecoder ->
                var finalMimeType = mimeType

                // Если плеер запрашивает декодер для Dolby Vision...
                if (MimeTypes.VIDEO_DOLBY_VISION == mimeType) {
                    // ...мы "обманываем" его и говорим системе искать декодер для HEVC.
                    Log.i("PlayerManager", "DV Fallback: Intercepted DV request, substituting with HEVC.")
                    finalMimeType = MimeTypes.VIDEO_H265
                }

                // Запрашиваем у системы декодеры для (возможно) подмененного MIME-типа.
                try {
                    MediaCodecUtil.getDecoderInfos(
                        finalMimeType,
                        requiresSecureDecoder,
                        requiresTunnelingDecoder
                    )
                } catch (e: MediaCodecUtil.DecoderQueryException) {
                    Log.e("PlayerManager", "Failed to query decoders for $finalMimeType", e)
                    emptyList()
                }
            }
        } else {
            MediaCodecSelector.DEFAULT
        }

        val trackSelector = DefaultTrackSelector(appContext)
        val parametersBuilder = trackSelector.buildUponParameters()
            .setAllowInvalidateSelectionsOnRendererCapabilitiesChange(true)
            .setTrackTypeDisabled(C.TRACK_TYPE_VIDEO, videoTrackDisabled)
            .setTunnelingEnabled(settingsRepo.isTunnelingEnabled())
            .setMaxVideoSize(MAX_UHD_VIDEO_WIDTH, MAX_UHD_VIDEO_HEIGHT)
            .setMaxVideoBitrate(Int.MAX_VALUE)
            .setViewportSize(MAX_UHD_VIDEO_WIDTH, MAX_UHD_VIDEO_HEIGHT, true)
            .setExceedVideoConstraintsIfNecessary(true)
            .setForceHighestSupportedBitrate(true)
            .setAllowVideoMixedMimeTypeAdaptiveness(true)
            .setAllowVideoNonSeamlessAdaptiveness(true)
            .setAllowVideoMixedDecoderSupportAdaptiveness(true)
            // Разрешаем плееру игнорировать битые дорожки
            .setExceedRendererCapabilitiesIfNecessary(true)
            .setAllowMultipleAdaptiveSelections(true)

        // Audio Language
        val audioPref = settingsRepo.getPreferredAudioLang()
        when (audioPref) {
            SettingsRepository.TRACK_DEFAULT -> parametersBuilder.setPreferredAudioLanguages()
            SettingsRepository.TRACK_DEVICE -> parametersBuilder.setPreferredAudioLanguages(*getDeviceLanguages())
            else -> parametersBuilder.setPreferredAudioLanguage(audioPref)
        }

        // Subtitle Language & CaptioningManager
        val captioningManager = appContext.getSystemService(Context.CAPTIONING_SERVICE) as? CaptioningManager
        if (captioningManager == null || !captioningManager.isEnabled) {
            parametersBuilder.setIgnoredTextSelectionFlags(C.SELECTION_FLAG_DEFAULT)
        }

        val subPref = settingsRepo.getPreferredSubLang()
        if (subPref == SettingsRepository.TRACK_DEVICE && captioningManager?.locale != null) {
            parametersBuilder.setPreferredTextLanguage(captioningManager.locale?.toLanguageTag())
        } else {
            when (subPref) {
                SettingsRepository.TRACK_DEFAULT -> parametersBuilder.setPreferredTextLanguages()
                SettingsRepository.TRACK_DEVICE -> parametersBuilder.setPreferredTextLanguages(*getDeviceLanguages())
                else -> parametersBuilder.setPreferredTextLanguage(subPref)
            }
        }

        trackSelector.setParameters(parametersBuilder)

        val requestedDecoderPriority = settingsRepo.getDecoderPriority()
        val effectiveDecoderPriority = when (requestedDecoderPriority) {
            DefaultRenderersFactory.EXTENSION_RENDERER_MODE_PREFER -> DefaultRenderersFactory.EXTENSION_RENDERER_MODE_ON
            else -> requestedDecoderPriority
        }
        if (effectiveDecoderPriority != requestedDecoderPriority) {
            Log.i(
                "DDDVR/PlayerManager",
                "VIDEO_DECODER_PRIORITY requested=$requestedDecoderPriority effective=$effectiveDecoderPriority reason=prefer_hardware_for_uhd_hdr"
            )
        } else {
            Log.i("DDDVR/PlayerManager", "VIDEO_DECODER_PRIORITY effective=$effectiveDecoderPriority")
        }

        val renderersFactory = object : DefaultRenderersFactory(appContext) {
            init {
                setMediaCodecSelector(mediaCodecSelector)
            }

            override fun buildVideoRenderers(
                context: Context,
                extensionRendererMode: Int,
                mediaCodecSelector: MediaCodecSelector,
                enableDecoderFallback: Boolean,
                eventHandler: Handler,
                eventListener: VideoRendererEventListener,
                allowedVideoJoiningTimeMs: Long,
                out: ArrayList<Renderer>
            ) {
                val startIndex = out.size
                super.buildVideoRenderers(
                    context,
                    extensionRendererMode,
                    mediaCodecSelector,
                    enableDecoderFallback,
                    eventHandler,
                    eventListener,
                    allowedVideoJoiningTimeMs,
                    out
                )

                val renderer = HdrMetadataVideoRenderer(
                    context,
                    getCodecAdapterFactory(),
                    mediaCodecSelector,
                    allowedVideoJoiningTimeMs,
                    enableDecoderFallback,
                    eventHandler,
                    eventListener,
                    MAX_DROPPED_VIDEO_FRAME_COUNT_TO_NOTIFY
                )
                for (index in startIndex until out.size) {
                    if (out[index] is MediaCodecVideoRenderer) {
                        out[index] = renderer
                        Log.i("DDDVR/PlayerManager", "VIDEO_RENDERER_HDR_METADATA installed index=$index")
                        return
                    }
                }

                out.add(startIndex, renderer)
                Log.w("DDDVR/PlayerManager", "VIDEO_RENDERER_HDR_METADATA inserted index=$startIndex")
            }

            override fun buildAudioRenderers(
                context: Context,
                extensionRendererMode: Int,
                mediaCodecSelector: MediaCodecSelector,
                enableDecoderFallback: Boolean,
                audioSink: AudioSink, // <-- Стандартный Sink от ExoPlayer
                eventHandler: Handler,
                eventListener: AudioRendererEventListener,
                out: ArrayList<Renderer>
            ) {
                // Решаем, какой Sink использовать
                val finalSink = if (settingsRepo.isStereoDownmixEnabled()) {
                    // Если нужен Downmix -> создаем свой Sink с процессором

                    // Ограничиваем аудиобуффер, чтобы не упасть по памяти
                    val bufferSizeProvider = DefaultAudioSink.AudioTrackBufferSizeProvider {
                            minSize, encoding, outputMode, pcmFrameSize, sampleRate, bitrate, speed ->

                        // Получаем стандартный размер, рассчитанный ExoPlayer
                        val standardSize = DefaultAudioSink.AudioTrackBufferSizeProvider.DEFAULT
                            .getBufferSizeInBytes(
                                minSize,
                                encoding,
                                outputMode,
                                pcmFrameSize,
                                sampleRate,
                                bitrate,
                                speed
                            )

                        standardSize.coerceAtMost(256 * 1024) // 256КБ должно хватить
                    }

                    val audioOutputProvider = AudioTrackAudioOutputProvider.Builder(appContext)
                        .setAudioTrackBufferSizeProvider(bufferSizeProvider)
                        .build()
                    //~ Ограничиваем аудиобуффер, чтобы не упасть по памяти

                    val sinkBuilder = DefaultAudioSink.Builder(appContext)
                        .setEnableAudioOutputPlaybackParameters(true)
                        .setEnableFloatOutput(false) // Важно для стабильности Downmix на старых чипах
                        .setAudioOutputProvider(audioOutputProvider) // Ограничиваем аудиобуффер, чтобы не упасть по памяти

                    val mixingProcessor = ChannelMixingAudioProcessor()
                    val matrices = AudioMixerLogic.createMatrices(settingsRepo)
                    matrices.forEach { matrix ->
                        mixingProcessor.putChannelMixingMatrix(matrix)
                    }

                    sinkBuilder.setAudioProcessorChain(
                        DefaultAudioSink.DefaultAudioProcessorChain(mixingProcessor)
                    )

                    sinkBuilder.build()
                } else {
                    // Если Downmix не нужен -> используем стандартный (для Passthrough и т.д.)
                    audioSink
                }

                super.buildAudioRenderers(
                    context,
                    extensionRendererMode,
                    mediaCodecSelector,
                    enableDecoderFallback,
                    finalSink,
                    eventHandler,
                    eventListener,
                    out
                )
            }
        }.apply {
            setExtensionRendererMode(effectiveDecoderPriority)
            setEnableDecoderFallback(true) // Разрешаем софтовый декодер
        }

        val loadControl = DefaultLoadControl.Builder()
            .setBufferDurationsMs(
                15000, // minBufferMs
                50000, // maxBufferMs
                500,   // bufferForPlaybackMs
                5000   // bufferForPlaybackAfterRebufferMs
            )
            .setPrioritizeTimeOverSizeThresholds(true)
            .build()

        // Build Player
        val player = ExoPlayer.Builder(appContext, renderersFactory)
            .setTrackSelector(trackSelector)
            .setLoadControl(loadControl)
            .setMediaSourceFactory(
                DefaultMediaSourceFactory(appContext, extractorsFactory)
                    .setDataSourceFactory(createParsingDataSourceFactory())
                    .setLoadErrorHandlingPolicy(loadErrorHandlingPolicy)
            )
            .build()

        // Audio Attributes
        val audioAttributes = AudioAttributes.Builder()
            .setUsage(C.USAGE_MEDIA)
            .setContentType(C.AUDIO_CONTENT_TYPE_MOVIE)
            .build()
        player.setAudioAttributes(audioAttributes, true)

        // Skip Silence
        if (settingsRepo.isSkipSilenceEnabled()) {
            player.skipSilenceEnabled = true
        }

        // Handle Noisy
        player.setHandleAudioBecomingNoisy(true)

        // Seek Parameters
        player.setSeekBackIncrementMs(15000)
        player.setSeekForwardIncrementMs(15000)

        // --- LoudnessEnhancer ---
        player.addListener(object : Player.Listener {
            override fun onAudioSessionIdChanged(audioSessionId: Int) {
                initLoudnessEnhancer(audioSessionId)
            }
        })
        if (player.audioSessionId != C.AUDIO_SESSION_ID_UNSET) {
            initLoudnessEnhancer(player.audioSessionId)
        }

        playerListener?.let { player.addListener(it) }

        // --- MediaSession ---
        if (player.canAdvertiseSession()) {
            try {
                mediaSession = MediaSession.Builder(context, player).build()
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }

        player.addAnalyticsListener(object : AnalyticsListener {
            override fun onVideoInputFormatChanged(
                eventTime: AnalyticsListener.EventTime,
                format: Format,
                decoderReuseEvaluation: androidx.media3.exoplayer.DecoderReuseEvaluation?
            ) {
                logVideoDecoderCandidates(format)
                Log.i(
                    "DDDVR/PlayerManager",
                    "VIDEO_INPUT_FORMAT width=${format.width} height=${format.height} sampleMime=${format.sampleMimeType} codecs=${format.codecs} colorInfo=${format.colorInfo} bitrate=${format.bitrate} decoderMode=$effectiveDecoderPriority"
                )
                onVideoFormatChanged?.invoke(format)
            }

            override fun onVideoDecoderInitialized(
                eventTime: AnalyticsListener.EventTime,
                decoderName: String,
                initializedTimestampMs: Long,
                initializationDurationMs: Long
            ) {
                Log.i(
                    "DDDVR/PlayerManager",
                    "VIDEO_DECODER_INITIALIZED name=$decoderName initMs=$initializationDurationMs"
                )
            }

            override fun onVideoCodecError(
                eventTime: AnalyticsListener.EventTime,
                videoCodecError: Exception
            ) {
                Log.e("DDDVR/PlayerManager", "VIDEO_CODEC_ERROR", videoCodecError)
            }

            override fun onAudioTrackInitialized(
                eventTime: AnalyticsListener.EventTime,
                config: AudioSink.AudioTrackConfig
            ) {
                // config.encoding говорит нам, в каком формате данные идут на железо.
                // Если это PCM (16-bit, Float), значит плеер декодировал звук.
                // Если это AC3, DTS и т.д., значит работает Passthrough.
                val encodingName = MediaFormatHelper.getAudioCodecName(config.encoding)
                val channelStr = MediaFormatHelper.getChannelConfigString(config.channelConfig)
                val passthrough = if (config.tunneling) " ↳" else ""
                val info = "$encodingName $channelStr$passthrough"

                onAudioOutputFormatChanged?.invoke(info)
            }
        })

        this.exoPlayer = player
        onPlayerCreated?.invoke(player)

        // 6. Restore State
        if (currentMediaItems.isNotEmpty()) {
            val sources = buildMediaSources(currentMediaItems)
            player.setMediaSources(sources, currentWindowIndex, currentPosition)
            player.playWhenReady = playWhenReady
            player.prepare()
            if (playWhenReady) {
                player.play()
            }
        }
    }

    fun setVideoTrackDisabled(disabled: Boolean, reason: String) {
        videoTrackDisabled = disabled
        val player = exoPlayer
        if (player == null) {
            Log.i(
                "DDDVR/PlayerManager",
                "VIDEO_TRACK_DISABLE_DEFERRED disabled=$disabled reason=$reason"
            )
            return
        }

        val builder = player.trackSelectionParameters.buildUpon()
            .setTrackTypeDisabled(C.TRACK_TYPE_VIDEO, disabled)
        if (!disabled) {
            builder.clearOverridesOfType(C.TRACK_TYPE_VIDEO)
        }
        player.trackSelectionParameters = builder.build()
        if (disabled) {
            player.clearVideoSurface()
        }
        Log.i(
            "DDDVR/PlayerManager",
            "VIDEO_TRACK_DISABLED disabled=$disabled reason=$reason"
        )
    }

    private fun buildMediaSources(exoItems: List<Media3MediaItem>): List<MediaSource> {
        // Передаем false вторым параметром. Это заставит плеер скачать первый .ts файл
        // и проанализировать его структуру, вместо того чтобы гадать по пустому m3u8.
        val hlsExtractorFactory = DefaultHlsExtractorFactory(tsExtractorFlags, false)

        val hlsMediaSourceFactory = HlsMediaSource.Factory(createParsingDataSourceFactory())
            .setExtractorFactory(hlsExtractorFactory)
            .setLoadErrorHandlingPolicy(loadErrorHandlingPolicy)

        val defaultMediaSourceFactory = DefaultMediaSourceFactory(appContext, extractorsFactory)
            .setDataSourceFactory(createParsingDataSourceFactory())
            .setLoadErrorHandlingPolicy(loadErrorHandlingPolicy)

        return exoItems.map { exoItem ->
            val uriStr = exoItem.localConfiguration?.uri?.toString() ?: ""
            val mimeType = resolvedMediaTypes[uriStr]
                ?: exoItem.localConfiguration?.mimeType
                ?: MediaFormatHelper.getVideoMimeType(uriStr.toUri())
            val isHls = mimeType == MimeTypes.APPLICATION_M3U8

            if (isHls) {
                hlsMediaSourceFactory.createMediaSource(exoItem)
            } else {
                defaultMediaSourceFactory.createMediaSource(exoItem)
            }
        }
    }

    private fun initLoudnessEnhancer(audioSessionId: Int) {
        try {
            loudnessEnhancer?.release()
            val boost = settingsRepo.getLoudnessBoost()
            if (boost > 0) {
                loudnessEnhancer = LoudnessEnhancer(audioSessionId)
                loudnessEnhancer?.setTargetGain(boost)
                loudnessEnhancer?.enabled = true
            } else {
                loudnessEnhancer = null
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    fun updateTrackSelectionParameters() {
        val player = exoPlayer ?: return
        val audioPref = settingsRepo.getPreferredAudioLang()
        val subPref = settingsRepo.getPreferredSubLang()

        val builder = player.trackSelectionParameters.buildUpon()

        when (audioPref) {
            SettingsRepository.TRACK_DEFAULT -> builder.setPreferredAudioLanguages()
            SettingsRepository.TRACK_DEVICE -> builder.setPreferredAudioLanguages(*getDeviceLanguages())
            else -> builder.setPreferredAudioLanguage(audioPref)
        }

        when (subPref) {
            SettingsRepository.TRACK_DEFAULT -> builder.setPreferredTextLanguages()
            SettingsRepository.TRACK_DEVICE -> builder.setPreferredTextLanguages(*getDeviceLanguages())
            else -> builder.setPreferredTextLanguage(subPref)
        }

        player.trackSelectionParameters = builder.build()
    }

    private fun getDeviceLanguages(): Array<String> {
        val locales = LocaleListCompat.getAdjustedDefault()
        val languages = mutableListOf<String>()
        for (i in 0 until locales.size()) {
            locales.get(i)?.language?.let { languages.add(it) }
        }
        return languages.toTypedArray()
    }

    fun getResolvedMimeType(uri: android.net.Uri): String? {
        return resolvedMediaTypes[uri.toString()]
    }

    fun loadPlaylist(items: List<MediaItem>, startIndex: Int, startPosMs: Long = 0) {
        currentTrackInfo = emptyMap()
        resolvedMediaTypes.clear() // Очищаем кэш типов при новой загрузке

        if (items.isNotEmpty()) {
            baseHttpFactory.setDefaultRequestProperties(items[startIndex].headers)
        }

        val exoItems = items.map { item ->
            val subConfigs = item.subtitles.map { sub ->
                Media3MediaItem.SubtitleConfiguration.Builder(sub.uri)
                    .setMimeType(sub.mimeType)
                    .setLanguage("ext")
                    .setLabel(sub.name ?: sub.filename)
                    .setSelectionFlags(C.SELECTION_FLAG_DEFAULT)
                    .build()
            }

            val metadata = MediaMetadata.Builder()
                .setTitle(item.title)
                .setArtworkUri(item.posterUri)
                .build()

            // Даем плееру больше времени на "переваривание" битых кадров,
            // отодвигая точку воспроизведения дальше от края трансляции.
            val liveConfig = Media3MediaItem.LiveConfiguration.Builder()
                .setTargetOffsetMs(15000) // увеличили до 15 сек
                .setMaxOffsetMs(30000)    // Максимальное отставание
                .setMaxPlaybackSpeed(1.05f) // Разрешаем ускоряться до 1.05x чтобы догнать поток
                .build()

            Media3MediaItem.Builder()
                .setUri(item.uri)
//                .setMimeType(mimeType)
                .setMediaMetadata(metadata)
                .setSubtitleConfigurations(subConfigs)
                .setLiveConfiguration(liveConfig)
                .build()
        }

        currentMediaItems = exoItems
        currentWindowIndex = startIndex
        currentPosition = if (startPosMs <= 0L) C.TIME_UNSET else startPosMs
        playWhenReady = true

        if (exoPlayer == null) {
            initializePlayer()
        } else {
            val sources = buildMediaSources(exoItems)
            exoPlayer?.setMediaSources(sources, startIndex, currentPosition)
            exoPlayer?.playWhenReady = true
            exoPlayer?.prepare()
            exoPlayer?.play()
        }
    }

    fun releasePlayer(isFinalRelease: Boolean = false, saveState: Boolean = true) {
        exoPlayer?.let { player ->
            if (saveState) {
                currentWindowIndex = player.currentMediaItemIndex
                currentPosition = player.currentPosition
                playWhenReady = player.playWhenReady
            }
            playerListener?.let { player.removeListener(it) }
            player.release()
        }
        mediaSession?.release()
        mediaSession = null
        loudnessEnhancer?.release()
        loudnessEnhancer = null
        exoPlayer = null

        // Зануляем коллбеки ТОЛЬКО если это полное уничтожение плеера (выход из приложения),
        // чтобы не сломать "горячую перезагрузку" при смене настроек.
        if (isFinalRelease) {
            playerListener = null
            onMetadataAvailable = null
            onPlayerCreated = null
            onVideoFormatChanged = null
            onAudioOutputFormatChanged = null
        }
    }

    private fun logVideoDecoderCandidates(format: Format) {
        val mimeType = format.sampleMimeType?.takeIf { it.isNotBlank() } ?: return
        val summary = runCatching {
            MediaCodecUtil.getDecoderInfos(mimeType, false, false)
                .take(8)
                .joinToString { info ->
                    val supported = runCatching { info.isFormatSupported(format) }.getOrDefault(false)
                    "${info.name}[supported=$supported]"
                }
        }.getOrElse { error ->
            "query_failed=${error::class.java.simpleName}:${error.message.orEmpty()}"
        }
        Log.i(
            "DDDVR/PlayerManager",
            "VIDEO_DECODER_CANDIDATES width=${format.width} height=${format.height} mime=$mimeType codecs=${format.codecs} colorInfo=${format.colorInfo} candidates=$summary"
        )
    }

    fun getTrackMetadata(): Map<Int, UnifiedMetadataReader.TrackInfo> = currentTrackInfo

    fun getAudioTrackRows(): List<OpenXrTrackRow> {
        val player = exoPlayer ?: return emptyList()
        val (options, selectedIndex) = TrackLogic.extractAudioTracks(player.currentTracks, currentTrackInfo)
        return options.mapIndexedNotNull { index, option ->
            if (option.isOff) null else option.toOpenXrTrackRow(prefix = "audio", index = index, selectedIndex = selectedIndex)
        }
    }

    fun getSubtitleTrackRows(): List<OpenXrTrackRow> {
        val player = exoPlayer ?: return emptyList()
        val (options, selectedIndex) = TrackLogic.extractSubtitleTracks(player.currentTracks, currentTrackInfo)
        return options.mapIndexed { index, option ->
            option.toOpenXrTrackRow(prefix = "subtitle", index = index, selectedIndex = selectedIndex)
        }
    }

    fun currentAudioTrackLabel(): String {
        val player = exoPlayer ?: return ""
        val (options, selectedIndex) = TrackLogic.extractAudioTracks(player.currentTracks, currentTrackInfo)
        return options.getOrNull(selectedIndex)?.let { buildOpenXrTrackTitle("audio", it, selectedIndex) }.orEmpty()
    }

    fun currentSubtitleTrackLabel(): String {
        val player = exoPlayer ?: return ""
        val (options, selectedIndex) = TrackLogic.extractSubtitleTracks(player.currentTracks, currentTrackInfo)
        return options.getOrNull(selectedIndex)?.let { buildOpenXrTrackTitle("subtitle", it, selectedIndex) }.orEmpty()
    }
    fun selectAudioTrack(id: String): Boolean =
        selectTrackById(id = id, prefix = "audio", trackType = C.TRACK_TYPE_AUDIO)

    fun selectSubtitleTrack(id: String): Boolean =
        selectTrackById(id = id, prefix = "subtitle", trackType = C.TRACK_TYPE_TEXT)

    fun disableSubtitles(): Boolean {
        val player = exoPlayer ?: return false
        val builder = player.trackSelectionParameters.buildUpon()
        builder.clearOverridesOfType(C.TRACK_TYPE_TEXT)
        builder.setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true)
        player.trackSelectionParameters = builder.build()
        Log.i("DDDVR/PlayerManager", "XR_TRACK_SELECT_APPLIED type=subtitle id=subtitle:off")
        return true
    }

    fun enableFirstSubtitleTrack(): Boolean {
        val player = exoPlayer ?: return false
        val (options, _) = TrackLogic.extractSubtitleTracks(player.currentTracks, currentTrackInfo)
        val firstSubtitle = options.firstOrNull { !it.isOff && it.groupIndex >= 0 }
            ?: options.firstOrNull { !it.isOff }
            ?: return false
        return selectSubtitleTrack(firstSubtitle.toOpenXrTrackId("subtitle", options.indexOf(firstSubtitle)))
    }

    private fun TrackOption.toOpenXrTrackId(prefix: String, index: Int): String {
        if (isOff) return "$prefix:off"
        return if (groupIndex >= 0 && trackIndex >= 0) {
            "$prefix:$groupIndex:$trackIndex"
        } else {
            "$prefix:$index"
        }
    }

    private fun TrackOption.toOpenXrTrackRow(
        prefix: String,
        index: Int,
        selectedIndex: Int
    ): OpenXrTrackRow {
        val supported = isOff || group?.isTrackSupported(trackIndex) == true
        return OpenXrTrackRow(
            id = toOpenXrTrackId(prefix, index),
            title = buildOpenXrTrackTitle(prefix, this, index),
            subtitle = buildOpenXrTrackSubtitle(prefix, this),
            selected = index == selectedIndex,
            enabled = supported
        )
    }

    private fun buildOpenXrTrackTitle(prefix: String, option: TrackOption, index: Int): String {
        if (option.isOff) return appContext.getString(top.rootu.dddvr.R.string.track_off)
        val format = option.format
        val number = option.index.takeIf { it > 0 } ?: index.coerceAtLeast(1)
        val language = languageLabel(format?.language)
        val explicitName = cleanTrackName(option)
        return when (prefix) {
            "audio" -> {
                val base = explicitName ?: language ?: "\u0414\u043E\u0440\u043E\u0436\u043A\u0430 $number"
                val tech = format?.let {
                    listOfNotNull(audioCodecLabel(it), channelLayoutLabel(it))
                        .distinct()
                        .joinToString(" ")
                }.orEmpty()
                if (tech.isBlank()) base else "$base - $tech"
            }
            "subtitle" -> explicitName ?: language ?: "\u0421\u0443\u0431\u0442\u0438\u0442\u0440\u044B $number"
            else -> explicitName ?: language ?: "Track $number"
        }
    }

    private fun buildOpenXrTrackSubtitle(prefix: String, option: TrackOption): String {
        val format = option.format ?: return ""
        val number = option.index.takeIf { it > 0 } ?: 0
        val parts = when (prefix) {
            "audio" -> emptyList()
            "subtitle" -> listOfNotNull(
                languageLabel(format.language),
                subtitleFormatLabel(format)
            )
            else -> listOfNotNull(languageLabel(format.language), format.sampleMimeType?.substringAfterLast('/'))
        }
        return parts.distinct().joinToString(" \u00B7 ")
    }

    private fun cleanTrackName(option: TrackOption): String? {
        val raw = option.nameFromMeta?.trim()?.takeIf { it.isNotBlank() }
            ?: option.format?.label?.trim()?.takeIf { it.isNotBlank() }
            ?: return null
        return raw.takeIf { !isGenericOpenXrTrackName(it) }
    }

    private fun isGenericOpenXrTrackName(value: String): Boolean {
        val normalized = value.trim().lowercase()
        if (normalized.isBlank()) return true
        if (normalized.contains("\u0437\u0432\u0443\u043A\u043E\u0432\u0430\u044F \u0434\u043E\u0440\u043E\u0436\u043A\u0430")) return true
        if (normalized.startsWith("audio") || normalized.startsWith("track ")) return true
        return normalized in setOf(
            "ru", "rus", "russian", "\u0440\u0443\u0441\u0441\u043A\u0438\u0439", "\u0440\u0443\u0441\u0441\u043A\u0430\u044F",
            "en", "eng", "english", "\u0430\u043D\u0433\u043B\u0438\u0439\u0441\u043A\u0438\u0439",
            "und", "unknown", "default"
        )
    }

    private fun languageLabel(language: String?): String? {
        val normalized = language?.trim()?.lowercase()?.takeIf { it.isNotBlank() } ?: return null
        if (normalized == "und" || normalized == "ext") return null
        return when (normalized) {
            "ru", "rus" -> "\u0420\u0443\u0441\u0441\u043A\u0438\u0439"
            "en", "eng" -> "\u0410\u043D\u0433\u043B\u0438\u0439\u0441\u043A\u0438\u0439"
            "uk", "ukr" -> "\u0423\u043A\u0440\u0430\u0438\u043D\u0441\u043A\u0438\u0439"
            "de", "deu", "ger" -> "\u041D\u0435\u043C\u0435\u0446\u043A\u0438\u0439"
            "fr", "fra", "fre" -> "\u0424\u0440\u0430\u043D\u0446\u0443\u0437\u0441\u043A\u0438\u0439"
            "es", "spa" -> "\u0418\u0441\u043F\u0430\u043D\u0441\u043A\u0438\u0439"
            "it", "ita" -> "\u0418\u0442\u0430\u043B\u044C\u044F\u043D\u0441\u043A\u0438\u0439"
            "ja", "jpn" -> "\u042F\u043F\u043E\u043D\u0441\u043A\u0438\u0439"
            "ko", "kor" -> "\u041A\u043E\u0440\u0435\u0439\u0441\u043A\u0438\u0439"
            "zh", "zho", "chi" -> "\u041A\u0438\u0442\u0430\u0439\u0441\u043A\u0438\u0439"
            else -> normalized.uppercase()
        }
    }
    private fun audioCodecLabel(format: Format): String? {
        return when (format.sampleMimeType) {
            MimeTypes.AUDIO_AC3 -> "AC3"
            MimeTypes.AUDIO_E_AC3 -> "E-AC3"
            MimeTypes.AUDIO_E_AC3_JOC -> "E-AC3 JOC"
            MimeTypes.AUDIO_DTS -> "DTS"
            MimeTypes.AUDIO_DTS_HD -> "DTS-HD"
            MimeTypes.AUDIO_DTS_EXPRESS -> "DTS-X"
            MimeTypes.AUDIO_TRUEHD -> "TrueHD"
            MimeTypes.AUDIO_AAC -> "AAC"
            MimeTypes.AUDIO_MPEG -> "MP3"
            MimeTypes.AUDIO_FLAC -> "FLAC"
            MimeTypes.AUDIO_OPUS -> "Opus"
            MimeTypes.AUDIO_VORBIS -> "Vorbis"
            MimeTypes.AUDIO_RAW -> "PCM"
            else -> format.sampleMimeType?.substringAfterLast('/')?.uppercase()
        }?.takeIf { it.isNotBlank() }
    }

    private fun subtitleFormatLabel(format: Format): String? {
        return when (format.sampleMimeType) {
            MimeTypes.APPLICATION_SUBRIP -> "SRT"
            MimeTypes.TEXT_VTT -> "VTT"
            MimeTypes.TEXT_SSA -> "SSA"
            MimeTypes.APPLICATION_TTML -> "TTML"
            MimeTypes.APPLICATION_MP4VTT -> "VTT"
            MimeTypes.APPLICATION_PGS -> "PGS"
            MimeTypes.APPLICATION_VOBSUB -> "VobSub"
            MimeTypes.APPLICATION_DVBSUBS -> "DVB"
            else -> format.sampleMimeType?.substringAfterLast('/')?.uppercase()
        }?.takeIf { it.isNotBlank() }
    }

    private fun channelLayoutLabel(format: Format): String? {
        return when (format.channelCount) {
            1 -> "1.0"
            2 -> "2.0"
            3 -> "2.1"
            4 -> "4.0"
            5 -> "5.0"
            6 -> "5.1"
            7 -> "6.1"
            8 -> "7.1"
            Format.NO_VALUE, 0 -> null
            else -> if (format.channelCount > 0) "${format.channelCount}ch" else null
        }
    }

    private fun bitrateLabel(format: Format): String? {
        return if (format.bitrate != Format.NO_VALUE && format.bitrate > 0) {
            "${format.bitrate / 1000}k"
        } else {
            null
        }
    }
    private data class TrackSelectionTarget(
        val id: String,
        val isOff: Boolean,
        val group: Tracks.Group? = null,
        val trackIndex: Int = -1,
        val option: TrackOption? = null
    )

    private fun trackOptionsFor(player: ExoPlayer, trackType: Int): List<TrackOption>? {
        return when (trackType) {
            C.TRACK_TYPE_AUDIO -> TrackLogic.extractAudioTracks(player.currentTracks, currentTrackInfo).first
            C.TRACK_TYPE_TEXT -> TrackLogic.extractSubtitleTracks(player.currentTracks, currentTrackInfo).first
            else -> null
        }
    }

    private fun resolveTrackSelectionTarget(
        player: ExoPlayer,
        id: String,
        prefix: String,
        trackType: Int
    ): TrackSelectionTarget? {
        val expectedPrefix = "$prefix:"
        if (!id.startsWith(expectedPrefix)) return null
        val body = id.removePrefix(expectedPrefix)
        if (body == "off") return TrackSelectionTarget(id = "$prefix:off", isOff = true)

        val stableParts = body.split(':')
        if (stableParts.size == 2) {
            val groupIndex = stableParts[0].toIntOrNull() ?: return null
            val trackIndex = stableParts[1].toIntOrNull() ?: return null
            val group = player.currentTracks.groups.getOrNull(groupIndex) ?: return null
            if (group.type != trackType || trackIndex !in 0 until group.length) return null
            val option = trackOptionsFor(player, trackType)
                ?.firstOrNull { it.groupIndex == groupIndex && it.trackIndex == trackIndex }
            return TrackSelectionTarget(
                id = "$prefix:$groupIndex:$trackIndex",
                isOff = false,
                group = group,
                trackIndex = trackIndex,
                option = option
            )
        }

        val legacyIndex = body.toIntOrNull() ?: return null
        val options = trackOptionsFor(player, trackType) ?: return null
        val option = options.getOrNull(legacyIndex) ?: return null
        if (option.isOff) return TrackSelectionTarget(id = "$prefix:off", isOff = true, option = option)
        val group = option.group ?: return null
        return TrackSelectionTarget(
            id = option.toOpenXrTrackId(prefix, legacyIndex),
            isOff = false,
            group = group,
            trackIndex = option.trackIndex,
            option = option
        )
    }

    private fun selectTrackById(id: String, prefix: String, trackType: Int): Boolean {
        val player = exoPlayer ?: return false
        val target = resolveTrackSelectionTarget(player, id, prefix, trackType) ?: run {
            Log.w("DDDVR/PlayerManager", "XR_TRACK_SELECT_IGNORED type=$prefix id=$id reason=unresolved")
            return false
        }
        val builder = player.trackSelectionParameters.buildUpon()
        builder.clearOverridesOfType(trackType)
        if (target.isOff) {
            builder.setTrackTypeDisabled(trackType, true)
        } else {
            val group = target.group ?: return false
            if (!group.isTrackSupported(target.trackIndex)) {
                Log.w("DDDVR/PlayerManager", "XR_TRACK_SELECT_IGNORED type=$prefix id=${target.id} reason=unsupported")
                return false
            }
            builder.setTrackTypeDisabled(trackType, false)
            builder.setOverrideForType(
                TrackSelectionOverride(group.mediaTrackGroup, target.trackIndex)
            )
        }
        player.trackSelectionParameters = builder.build()
        val label = target.option?.let { buildOpenXrTrackTitle(prefix, it, it.index) }.orEmpty()
        val subtitle = target.option?.let { buildOpenXrTrackSubtitle(prefix, it) }.orEmpty()
        Log.i("DDDVR/PlayerManager", "XR_TRACK_SELECT_APPLIED type=$prefix id=${target.id} label=$label subtitle=$subtitle")
        return true
    }
    fun togglePlayPause() {
        exoPlayer?.let { if (it.isPlaying) it.pause() else it.play() }
    }

    fun seekForward() {
        exoPlayer?.let { it.seekTo((it.currentPosition + it.seekForwardIncrement).coerceAtMost(it.duration)) }
    }

    fun seekBack() {
        exoPlayer?.let { it.seekTo((it.currentPosition - it.seekBackIncrement).coerceAtLeast(0)) }
    }
}
