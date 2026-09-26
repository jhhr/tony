
project(
  'Tony',
  'c', 'cpp',
  version: '3.0.0-pre1',
  license: 'GPL-2.0-or-later',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'buildtype=release',
    'b_ndebug=if-release',
  ],
  meson_version: '>= 0.57.0'
)

conf = configuration_data()
conf.set('version', meson.project_version())
configure_file(input: 'version.h.in', output: 'version.h', configuration: conf)

qt = import('qt6')

qt_dep = dependency(
  'qt6',
  modules: [
    'Core', 'Gui', 'Widgets', 'Xml', 'Network', 'Svg', 'Test',
  ],
  main: true
)

system = host_machine.system()
architecture = host_machine.cpu_family()
compiler = meson.get_compiler('cpp').get_id()
buildtype = get_option('buildtype')

general_defines = [
]

general_link_args = []

config_summary = {
  'Compiler': compiler,
  'Architecture': architecture,
  'System': system,
  'Build type': buildtype
}

if buildtype.startswith('release')
  general_defines += [
    '-DNO_TIMING',
    '-DNO_HIT_COUNTS',
  ]
  if compiler == 'clang'
    general_defines += [
      '-Wno-deprecated-copy', # Qt currently hits lots of these internally
    ]
  elif compiler == 'gcc'
    general_defines += [
    ]
  elif compiler == 'msvc'
    general_defines += [
      '-wd4068', # unknown pragma (we have lots of pragma 'GCC')
      '-wd4267', # conversion from size_t to smaller integral type
      '-wd4996', # deprecated functions, including annoying warnings about POSIX
      '-Zc:twoPhase-', # work around incompatibility with some SDKs (CI only)
    ]
  endif # compiler
elif buildtype.startswith('debug')
  general_defines += [
    '-DWANT_TIMING',
    '-DWANT_HIT_COUNTS',
  ]
endif # get_option('buildtype')

# The development checks (main/dev/, docs/calibrate-audio.md) are in
# every build but a release one: debug, debugoptimized, plain, minsize
# and custom alike.  Packages are release builds and have none of it.
# moc has to see the flag too, for what is inside its #ifdefs
dev_checks = not buildtype.startswith('release')
dev_moc_args = []
if dev_checks
  general_defines += [
    '-DTONY_DEV_CHECKS',
  ]
  dev_moc_args += [
    '-DTONY_DEV_CHECKS',
  ]
endif

svcore_moc_args = []
feature_additional_libs = []
feature_include_dirs = []
vamp_symbol_args = []

dl_dep = meson.get_compiler('c').find_library('dl', required: false)
vamphostsdk_dep = dependency('vamp-hostsdk', version: '>= 2.10', required: false)
boost_dep = dependency('boost')

if system == 'linux'

  rc = []

  bzip2_dep = dependency('bzip2', required: false)

  if not bzip2_dep.found()
    message('Failed to find bzip2 dependency. On many systems this exists but is not discoverable through pkg-config; proceeding on this assumption')
    feature_additional_libs = [ '-lbz2' ]
  endif # bzip2_dep

  fftw3_dep = dependency('fftw3', version: '>= 3.0.0')
  sndfile_dep = dependency('sndfile', version: '>= 1.0.16')
  samplerate_dep = dependency('samplerate', version: '>= 0.1.2')
  rubberband_dep = dependency('rubberband', version: '>= 3.0.0')
  sord_dep = dependency('sord-0', version: '>= 0.5')
  serd_dep = dependency('serd-0', version: '>= 0.5')
  oggz_dep = dependency('oggz', version: '>= 1.0.0')
  fishsound_dep = dependency('fishsound', version: '>= 1.0.0')
  mad_dep = dependency('mad', version: '>= 0.15.0')
  id3tag_dep = dependency('id3tag', version: '>= 0.15.0')
  opus_dep = dependency('opusfile')
  opusenc_dep = dependency('opusenc', required: false)
  if not opusenc_dep.found()
    opusenc_dep = dependency('libopusenc', required: false)
  endif
  jack_dep = dependency('jack', version: '>= 0.100')
  libpulse_dep = dependency('libpulse', version: '>= 0.9')
  alsa_dep = dependency('alsa')

  portaudio_dep = dependency('portaudio-2.0', version: '>= 19', required: false)

  feature_dependencies = [
    vamphostsdk_dep,
    bzip2_dep,
    fftw3_dep,
    sndfile_dep,
    samplerate_dep,
    rubberband_dep,
    sord_dep,
    serd_dep,
    oggz_dep,
    fishsound_dep,
    mad_dep,
    id3tag_dep,
    opus_dep,
    opusenc_dep,
    portaudio_dep,
    jack_dep,
    libpulse_dep,
    alsa_dep,
  ]

  feature_defines = [
    '-DHAVE_BZ2',
    '-DHAVE_FFTW3',
    '-DFFTW_DOUBLE_ONLY',
    '-DHAVE_SNDFILE',
    '-DHAVE_LIBSAMPLERATE',
    '-DHAVE_RUBBERBAND',
    '-DHAVE_SORD', '-DUSE_SORD',
    '-DHAVE_SERD',
    '-DHAVE_OGGZ',
    '-DHAVE_FISHSOUND',
    '-DHAVE_MAD',
    '-DHAVE_ID3TAG',
    '-DHAVE_OPUS',
    '-DHAVE_JACK', '-DDYNAMIC_JACK',
    '-DHAVE_LIBPULSE',
    '-D__LINUX_ALSASEQ__',
    '-D__LINUX_ALSA__' # for RtMidi
  ]

  if portaudio_dep.found()
    feature_defines += [
      '-DHAVE_PORTAUDIO',
    ]
  endif

  if not opusenc_dep.found()
    feature_defines += [
      '-DHAVE_OPUS_READ_ONLY'
    ]
  endif

  svcore_moc_args = [
    '-DHAVE_MAD'
  ]

  vamp_symbol_args += [
    '-Wl,--version-script=' + meson.current_source_dir() / 'vamp-plugin-sdk/vamp-plugin.map'
  ]

elif system == 'darwin'

  svdeps_dir = meson.current_source_dir() / 'sv-dependency-builds/osx'

  if architecture == 'aarch64'
    svdeps_libdir = svdeps_dir / 'lib-arm64'
  else
    svdeps_libdir = svdeps_dir / 'lib'
  endif

  rc = []

  general_defines += [
    '-mmacosx-version-min=10.15'
  ]
  general_link_args += [
    '-mmacosx-version-min=10.15'
  ]

  feature_defines = [
    '-DHAVE_BZ2',
    '-DHAVE_SNDFILE',
    '-DHAVE_LIBSAMPLERATE',
    '-DHAVE_RUBBERBAND',
    '-DHAVE_SORD', '-DUSE_SORD',
    '-DHAVE_SERD',
    '-DHAVE_OPUS',
    '-DHAVE_OPUS_READ_ONLY',
    '-DHAVE_PORTAUDIO',
    '-DHAVE_COREAUDIO',
    '-DHAVE_VDSP',
    '-D__MACOSX_CORE__', # for RtMidi
  ]

  # vDSP will be used in preference to FFTW most of the time, but
  # FFTW supports more fft lengths - without it we can end up with a
  # very slow implementation for unusual lengths.
  feature_defines += [
    '-DHAVE_FFTW3',
    '-DFFTW_DOUBLE_ONLY',
  ]

  if architecture != 'aarch64'

    # We don't actually need MAD on this platform at all - CoreAudio
    # can read mp3 files no problem - but we have been using it up to
    # now, so for consistency let's stick with it. No need to bring it
    # along to aarch64 with us though.
    feature_defines += [
      '-DHAVE_MAD',
      '-DHAVE_ID3TAG',
    ]
    svcore_moc_args += [
      '-DHAVE_MAD',
    ]

  endif

  feature_dependencies = [
    vamphostsdk_dep,
  ]

  feature_include_dirs = [
    # These must be relative, so not using svdeps_dir
    'sv-dependency-builds/osx/include-arm64',
    'sv-dependency-builds/osx/include',
    'sv-dependency-builds/osx/include/opus',
  ]

  feature_additional_libs = [
    '-L' + svdeps_libdir,
    '-lbz2',
    '-lrubberband',
    '-lsndfile',
    '-lFLAC',
    '-logg',
    '-lvorbis',
    '-lvorbisenc',
    '-lvorbisfile',
    '-lopusfile',
    '-lopus',
    '-lportaudio',
    '-lsamplerate',
    '-lfftw3',
    '-lz',
    '-lsord-0',
    '-lserd-0',
    '-framework', 'CoreAudio',
    '-framework', 'CoreMidi',
    '-framework', 'AudioUnit',
    '-framework', 'AudioToolbox',
    '-framework', 'CoreFoundation',
    '-framework', 'CoreServices',
    '-framework', 'Accelerate',
  ]

  if architecture != 'aarch64'
    # See comments when setting up feature_defines above
    feature_additional_libs += [
      '-lmad',
      '-lid3tag',
    ]
  endif

  vamp_symbol_args += [
    '-exported_symbols_list', meson.current_source_dir() / 'vamp-plugin-sdk/vamp-plugin.list'
  ]

elif system == 'windows'

  if architecture == 'x86'
    svdeps_dir = meson.current_source_dir() / 'sv-dependency-builds/win32-mingw'
  elif compiler == 'msvc'
    svdeps_dir = meson.current_source_dir() / 'sv-dependency-builds/win64-msvc'
  else
    # MinGW/GCC x86_64: use system libraries from msys2, no svdeps needed
    svdeps_dir = ''
  endif # architecture / compiler

  windows = import('windows')
  rc = windows.compile_resources('icons/tony.rc')

  general_defines += [
    '-DWIN32',
    '-DNOMINMAX',
    '-DUNICODE',
    '-D_USE_MATH_DEFINES',
    '-D_HAS_STD_BYTE=1',
  ]

  if compiler != 'msvc'
    # MinGW + C++17: rpcndr.h (via <windows.h>) unconditionally typedefs
    # "unsigned char byte", which collides with the C++17 std::byte enum.
    # Fix by force-including a header that pre-includes <windows.h> with
    # WIN32_LEAN_AND_MEAN (which skips rpcndr.h), setting the include
    # guard so that subsequent #include <windows.h> directives are no-ops
    # and rpcndr.h is never processed.
    _fix_header = meson.current_source_dir() / 'main/mingw_byte_fix.h'
    general_defines += [
      '-include', _fix_header,
    ]
  endif

  feature_defines = [
    '-DHAVE_BZ2',
    '-DHAVE_FFTW3',
    '-DFFTW_DOUBLE_ONLY',
    '-DHAVE_SNDFILE',
    '-DHAVE_LIBSAMPLERATE',
    '-DHAVE_RUBBERBAND',
    '-DHAVE_SORD', '-DUSE_SORD',
    '-DHAVE_SERD',
    '-DHAVE_MAD',
    '-DHAVE_ID3TAG',
    '-DHAVE_OPUS',
    '-DHAVE_OPUS_READ_ONLY',
    '-DHAVE_PORTAUDIO',
    '-D__WINDOWS_MM__', # for RtMidi
  ]

  feature_dependencies = [
    vamphostsdk_dep,
  ]

  if architecture == 'x86'
    feature_include_dirs = [
      # These must be relative, so not using svdeps_dir
      'sv-dependency-builds/win32-mingw/include',
      'sv-dependency-builds/win32-mingw/include/opus',
    ]
  elif compiler == 'msvc'
    # MSVC x86_64: use pre-built dependency tree and MediaFoundation
    feature_defines += [
      '-DHAVE_MEDIAFOUNDATION',
    ]
    feature_include_dirs = [
      # These must be relative, so not using svdeps_dir
      'sv-dependency-builds/win64-msvc/include',
      'sv-dependency-builds/win64-msvc/include/opus',
    ]
  else
    # MinGW/GCC x86_64: system headers from msys2.
    # MediaFoundation IS available in mingw-w64 headers (mfapi.h, mfidl.h,
    # mfreadwrite.h, etc. all ship with the mingw-w64 packages in msys2).
    # The only header that was missing was shobjidl_core.h — mingw-w64 ships
    # the umbrella shobjidl.h instead.  MediaFoundationReadStream.cpp now
    # includes <shobjidl.h> when built with __MINGW32__, so we can enable
    # HAVE_MEDIAFOUNDATION here too and get .m4a / .aac / .mp4 support.
    feature_defines += [
      '-DHAVE_MEDIAFOUNDATION',
    ]
    # Several system headers live in versioned subdirectories that code
    # includes without a path prefix, so we add them explicitly.
    # Use run_command to resolve the actual Windows path for the mingw prefix.
    _mingw_prefix = run_command('sh', '-c', 'echo $MINGW_PREFIX', check: true).stdout().strip()
    if _mingw_prefix == ''
      _mingw_prefix = 'C:/msys64/mingw64'
    endif
    feature_include_dirs = [
      _mingw_prefix + '/include/opus',       # opusfile.h includes <opus_multistream.h>
      _mingw_prefix + '/include/sord-0',     # dataquay includes <sord/sord.h>
      _mingw_prefix + '/include/serd-0',     # dataquay includes <serd/serd.h>
    ]
  endif # architecture / compiler

  if architecture == 'x86' or compiler == 'msvc'
    if buildtype.startswith('release')
      feature_additional_libpaths = [
        '-L' + svdeps_dir / 'lib',
      ]
    else
      feature_additional_libpaths = [
        '-L' + svdeps_dir / 'lib/debug',
        '-L' + svdeps_dir / 'lib',
      ]
    endif
  else
    # MinGW x86_64: all libs come from the msys2 system prefix
    feature_additional_libpaths = []
  endif

  feature_additional_libs = [
    feature_additional_libpaths,
    '-lbz2',
    '-lrubberband',
    '-lfftw3',
    '-lsndfile',
    '-logg',
    '-lopusfile',
    '-lopus',
    '-lmad',
    '-lid3tag',
    '-lportaudio',
    '-lsamplerate',
    '-lz',
  ]

  if architecture == 'x86'
    feature_additional_libs += [
      '-lFLAC',
      '-lvorbis',
      '-lvorbisenc',
      '-lvorbisfile',
      '-lsord-0',
      '-lserd-0',
      # (We don't have MediaFoundation support with this build sadly)
      '-lwinmm',
      '-lws2_32',
    ]
  elif compiler == 'msvc'
    # MSVC x86_64 with MediaFoundation
    feature_additional_libs += [
#      '-NODEFAULTLIB:LIBCMT',
      '-lsord',
      '-lserd',
      '-lmfplat',
      '-lmfreadwrite',
      '-lmfuuid',
      '-lpropsys',
      '-ladvapi32',
      '-lwinmm',
      '-lws2_32',
    ]
  elif architecture == 'x86_64'
    # MinGW x86_64: sord/serd have a -0 suffix in the msys2 packages.
    # MediaFoundation libraries are available from the mingw-w64 runtime.
    feature_additional_libs += [
      '-lsord-0',
      '-lserd-0',
      '-lmfplat',
      '-lmfreadwrite',
      '-lmfuuid',
      '-lpropsys',
      '-ladvapi32',
      '-lwinmm',
      '-lws2_32',
    ]
  else
    error('Build for architecture ' + architecture + ' is not supported on this platform')
  endif # architecture / compiler

  svcore_moc_args = [
    '-DHAVE_MAD',
    '-DQ_OS_WIN',
  ]

  if compiler == 'msvc'
    vamp_symbol_args += [
      '-EXPORT:vampGetPluginDescriptor'
    ]
  else
    # MinGW/GCC: use GNU ld syntax to export the Vamp entry point
    vamp_symbol_args += [
      '-Wl,--export-dynamic-symbol=vampGetPluginDescriptor'
    ]
  endif

elif system == 'android'

  # Cross-compiled with the NDK: see deploy/android/build-tony.sh. The
  # libraries are static builds from deploy/android/build-deps.sh, found
  # through pkg-config; static: true, because only then does pkg-config
  # name their private libraries (ogg, opus, zix, serd) as well. As on
  # Linux, less what a phone has no use for: JACK, PulseAudio, ALSA and
  # PortAudio (with RtMidi's ALSA defines), and oggz and fishsound (Ogg
  # Vorbis). Opus is read-only: there is no libopusenc.

  if architecture == 'aarch64'
    android_abi = 'arm64-v8a'
  else
    error('Build for architecture ' + architecture + ' is not supported on this platform')
  endif # architecture

  rc = []

  bzip2_dep = dependency('bzip2', static: true)
  fftw3_dep = dependency('fftw3', version: '>= 3.0.0', static: true)
  sndfile_dep = dependency('sndfile', version: '>= 1.0.16', static: true)
  samplerate_dep = dependency('samplerate', version: '>= 0.1.2', static: true)
  rubberband_dep = dependency('rubberband', version: '>= 3.0.0', static: true)
  sord_dep = dependency('sord-0', version: '>= 0.5', static: true)
  serd_dep = dependency('serd-0', version: '>= 0.5', static: true)
  mad_dep = dependency('mad', version: '>= 0.15.0', static: true)
  id3tag_dep = dependency('id3tag', version: '>= 0.15.0', static: true)
  opus_dep = dependency('opusfile', static: true)
  # The audio device: bqaudioio has no Android backend (main/OboeAudioIO)
  oboe_dep = dependency('oboe', static: true)

  feature_dependencies = [
    vamphostsdk_dep,
    bzip2_dep,
    fftw3_dep,
    sndfile_dep,
    samplerate_dep,
    rubberband_dep,
    sord_dep,
    serd_dep,
    mad_dep,
    id3tag_dep,
    opus_dep,
    oboe_dep,
  ]

  feature_defines = [
    '-DHAVE_BZ2',
    '-DHAVE_FFTW3',
    '-DFFTW_DOUBLE_ONLY',
    '-DHAVE_SNDFILE',
    '-DHAVE_LIBSAMPLERATE',
    '-DHAVE_RUBBERBAND',
    '-DHAVE_SORD', '-DUSE_SORD',
    '-DHAVE_SERD',
    '-DHAVE_MAD',
    '-DHAVE_ID3TAG',
    '-DHAVE_OPUS',
    '-DHAVE_OPUS_READ_ONLY',
  ]

  svcore_moc_args = [
    '-DHAVE_MAD'
  ]

  vamp_symbol_args += [
    '-Wl,--version-script=' + meson.current_source_dir() / 'vamp-plugin-sdk/vamp-plugin.map'
  ]

  # Android's system log, which main.cpp passes stdout and stderr on to;
  # and its media extractors and decoders (main/AndroidMediaReadStream)
  feature_additional_libs = [
    '-llog',
    '-lmediandk',
  ]

else
    error('This operating system ("' + system + '") is not supported by this build file')
endif # system

all_include_dirs = include_directories(
  [
    'bqvec',
    'bqvec/bqvec',
    'bqfft',
    'bqresample',
    'bqaudioio',
    'bqaudioio/bqaudioio',
    'bqaudiostream',
    'bqaudiostream/bqaudiostream',
    'bqthingfactory',
    'piper-vamp-cpp',
    'checker',
    'checker/src',
    'checker/checker',
    'dataquay',
    'dataquay/dataquay',
    'svcore',
    'svcore/data',
    'svcore/plugin/api/alsa',
    'svcore',
    'svgui',
    'svapp',
    'vamp-plugin-sdk',
  ]
)

bq_files = [
  'bqvec/src/Allocators.cpp',
  'bqvec/src/Barrier.cpp',
  'bqvec/src/VectorOpsComplex.cpp',
  'bqfft/src/FFT.cpp',
  'bqresample/src/Resampler.cpp',
  'bqaudioio/src/AudioFactory.cpp',
  'bqaudioio/src/JACKAudioIO.cpp',
  'bqaudioio/src/Log.cpp',
  'bqaudioio/src/PortAudioIO.cpp',
  'bqaudioio/src/PulseAudioIO.cpp',
  'bqaudioio/src/ResamplerWrapper.cpp',
  'bqaudioio/src/SystemPlaybackTarget.cpp',
  'bqaudioio/src/SystemRecordSource.cpp',
  'bqaudiostream/src/AudioReadStream.cpp',
  'bqaudiostream/src/AudioReadStreamFactory.cpp',
  'bqaudiostream/src/AudioWriteStreamFactory.cpp',
  'bqaudiostream/src/AudioStreamExceptions.cpp',
]

if vamphostsdk_dep.found()
  config_summary += { 'Vamp SDK': 'system' }
  vamphostsdk_files = []
else
  config_summary += { 'Vamp SDK': 'bundled' }
  vamphostsdk_files = [
    'vamp-plugin-sdk/src/vamp-hostsdk/PluginBufferingAdapter.cpp',
    'vamp-plugin-sdk/src/vamp-hostsdk/PluginChannelAdapter.cpp',
    'vamp-plugin-sdk/src/vamp-hostsdk/PluginHostAdapter.cpp',
    'vamp-plugin-sdk/src/vamp-hostsdk/PluginInputDomainAdapter.cpp',
    'vamp-plugin-sdk/src/vamp-hostsdk/PluginLoader.cpp',
    'vamp-plugin-sdk/src/vamp-hostsdk/PluginSummarisingAdapter.cpp',
    'vamp-plugin-sdk/src/vamp-hostsdk/PluginWrapper.cpp',
    'vamp-plugin-sdk/src/vamp-hostsdk/RealTime.cpp',
    'vamp-plugin-sdk/src/vamp-hostsdk/Files.cpp',
  ]
endif

dataquay_files = [
  'dataquay/src/Connection.cpp',
  'dataquay/src/Node.cpp',
  'dataquay/src/PropertyObject.cpp',
  'dataquay/src/RDFException.cpp',
  'dataquay/src/Store.cpp',
  'dataquay/src/Transaction.cpp',
  'dataquay/src/TransactionalStore.cpp',
  'dataquay/src/Triple.cpp',
  'dataquay/src/Uri.cpp',
  'dataquay/src/backend/BasicStoreRedland.cpp',
  'dataquay/src/backend/BasicStoreSord.cpp',
  'dataquay/src/backend/define-check.cpp',
  'dataquay/src/objectmapper/ContainerBuilder.cpp',
  'dataquay/src/objectmapper/ObjectBuilder.cpp',
  'dataquay/src/objectmapper/ObjectLoader.cpp',
  'dataquay/src/objectmapper/ObjectMapper.cpp',
  'dataquay/src/objectmapper/ObjectMapperForwarder.cpp',
  'dataquay/src/objectmapper/ObjectStorer.cpp',
  'dataquay/src/objectmapper/TypeMapping.cpp',
]

dataquay_moc_files = qt.preprocess(
  moc_headers: [
  'dataquay/dataquay/Connection.h',
  'dataquay/dataquay/TransactionalStore.h',
  'dataquay/dataquay/objectmapper/ObjectMapper.h',
  'dataquay/dataquay/objectmapper/ObjectMapperForwarder.h',
])

svcore_files = [
  'svcore/base/AudioLevel.cpp',
  'svcore/base/ById.cpp',
  'svcore/base/Clipboard.cpp',
  'svcore/base/ColumnOp.cpp',
  'svcore/base/Command.cpp',
  'svcore/base/Debug.cpp',
  'svcore/base/EventSeries.cpp',
  'svcore/base/Exceptions.cpp',
  'svcore/base/HelperExecPath.cpp',
  'svcore/base/HitCount.cpp',
  'svcore/base/LogRange.cpp',
  'svcore/base/Pitch.cpp',
  'svcore/base/PlayParameterRepository.cpp',
  'svcore/base/PlayParameters.cpp',
  'svcore/base/Preferences.cpp',
  'svcore/base/Profiler.cpp',
  'svcore/base/ProgressPrinter.cpp',
  'svcore/base/ProgressReporter.cpp',
  'svcore/base/PropertyContainer.cpp',
  'svcore/base/RangeMapper.cpp',
  'svcore/base/RealTimeSV.cpp',
  'svcore/base/RecentFiles.cpp',
  'svcore/base/RecordDirectory.cpp',
  'svcore/base/ResourceFinder.cpp',
  'svcore/base/Selection.cpp',
  'svcore/base/Serialiser.cpp',
  'svcore/base/StorageAdviser.cpp',
  'svcore/base/StringBits.cpp',
  'svcore/base/Strings.cpp',
  'svcore/base/TempDirectory.cpp',
  'svcore/base/TempWriteFile.cpp',
  'svcore/base/TextMatcher.cpp',
  'svcore/base/Thread.cpp',
  'svcore/base/UnitDatabase.cpp',
  'svcore/base/ViewManagerBase.cpp',
  'svcore/base/XmlExportable.cpp',
  'svcore/base/ZoomLevel.cpp',
  'svcore/data/fileio/AudioFileReader.cpp',
  'svcore/data/fileio/AudioFileReaderFactory.cpp',
  'svcore/data/fileio/AudioFileSizeEstimator.cpp',
  'svcore/data/fileio/BQAFileReader.cpp',
  'svcore/data/fileio/BZipFileDevice.cpp',
  'svcore/data/fileio/CachedFile.cpp',
  'svcore/data/fileio/CodedAudioFileReader.cpp',
  'svcore/data/fileio/CSVFileReader.cpp',
  'svcore/data/fileio/CSVFileWriter.cpp',
  'svcore/data/fileio/CSVFormat.cpp',
  'svcore/data/fileio/DataFileReaderFactory.cpp',
  'svcore/data/fileio/DecodingWavFileReader.cpp',
  'svcore/data/fileio/FileReadThread.cpp',
  'svcore/data/fileio/FileSource.cpp',
  'svcore/data/fileio/MIDIFileReader.cpp',
  'svcore/data/fileio/MIDIFileWriter.cpp',
  'svcore/data/fileio/MP3FileReader.cpp',
  'svcore/data/fileio/PlaylistFileReader.cpp',
  'svcore/data/fileio/TextTest.cpp',
  'svcore/data/fileio/WavFileReader.cpp',
  'svcore/data/fileio/WavFileWriter.cpp',
  'svcore/data/midi/MIDIInput.cpp',
  'svcore/data/midi/rtmidi/RtMidi.cpp',
  'svcore/data/model/AggregateWaveModel.cpp',
  'svcore/data/model/AlignmentModel.cpp',
  'svcore/data/model/BasicCompressedDenseThreeDimensionalModel.cpp',
  'svcore/data/model/Dense3DModelPeakCache.cpp',
  'svcore/data/model/DenseTimeValueModel.cpp',
  'svcore/data/model/EditableDenseThreeDimensionalModel.cpp',
  'svcore/data/model/FFTModel.cpp',
  'svcore/data/model/Model.cpp',
  'svcore/data/model/ModelDataTableModel.cpp',
  'svcore/data/model/PowerOfSqrtTwoZoomConstraint.cpp',
  'svcore/data/model/PowerOfTwoZoomConstraint.cpp',
  'svcore/data/model/RangeSummarisableTimeValueModel.cpp',
  'svcore/data/model/RelativelyFineZoomConstraint.cpp',
  'svcore/data/model/WaveformOversampler.cpp',
  'svcore/data/model/WaveFileModel.cpp',
  'svcore/data/model/ReadOnlyWaveFileModel.cpp',
  'svcore/data/model/WritableWaveFileModel.cpp',
  'svcore/data/osc/OSCMessage.cpp',
  'svcore/data/osc/OSCQueue.cpp',
  'svcore/plugin/PluginScan.cpp',
  'svcore/plugin/DSSIPluginFactory.cpp',
  'svcore/plugin/DSSIPluginInstance.cpp',
  'svcore/plugin/FeatureExtractionPluginFactory.cpp',
  'svcore/plugin/LADSPAPluginFactory.cpp',
  'svcore/plugin/LADSPAPluginInstance.cpp',
  'svcore/plugin/NativeVampPluginFactory.cpp',
  'svcore/plugin/PiperVampPluginFactory.cpp',
  'svcore/plugin/PluginIdentifier.cpp',
  'svcore/plugin/PluginPathSetter.cpp',
  'svcore/plugin/PluginXml.cpp',
  'svcore/plugin/RealTimePluginFactory.cpp',
  'svcore/plugin/RealTimePluginInstance.cpp',
  'svcore/plugin/plugins/SamplePlayer.cpp',
  'svcore/rdf/PluginRDFIndexer.cpp',
  'svcore/rdf/PluginRDFDescription.cpp',
  'svcore/rdf/RDFExporter.cpp',
  'svcore/rdf/RDFFeatureWriter.cpp',
  'svcore/rdf/RDFImporter.cpp',
  'svcore/rdf/RDFTransformFactory.cpp',
  'svcore/system/Init.cpp',
  'svcore/system/System.cpp',
  'svcore/transform/CSVFeatureWriter.cpp',
  'svcore/transform/FeatureExtractionModelTransformer.cpp',
  'svcore/transform/FileFeatureWriter.cpp',
  'svcore/transform/RealTimeEffectModelTransformer.cpp',
  'svcore/transform/Transform.cpp',
  'svcore/transform/TransformFactory.cpp',
  'svcore/transform/ModelTransformer.cpp',
  'svcore/transform/ModelTransformerFactory.cpp',
  'checker/src/plugincandidates.cpp',
  'checker/src/knownplugincandidates.cpp',
  'checker/src/knownplugins.cpp'
]

if compiler != 'msvc' # i.e. build is not the 64-bit Windows one
  svcore_files += [
    'svcore/system/os-other.cpp',
  ]
endif

svcore_moc_files = qt.preprocess(
  moc_headers: [
  'svcore/base/Command.h',
  'svcore/base/PlayParameterRepository.h',
  'svcore/base/PlayParameters.h',
  'svcore/base/Preferences.h',
  'svcore/base/ProgressPrinter.h',
  'svcore/base/ProgressReporter.h',
  'svcore/base/PropertyContainer.h',
  'svcore/base/RecentFiles.h',
  'svcore/base/Thread.h',
  'svcore/base/UnitDatabase.h',
  'svcore/base/ViewManagerBase.h',
  'svcore/data/fileio/AudioFileReader.h',
  'svcore/data/fileio/BQAFileReader.h',
  'svcore/data/fileio/BZipFileDevice.h',
  'svcore/data/fileio/CodedAudioFileReader.h',
  'svcore/data/fileio/CSVFileWriter.h',
  'svcore/data/fileio/DecodingWavFileReader.h',
  'svcore/data/fileio/FileReadThread.h',
  'svcore/data/fileio/FileSource.h',
  'svcore/data/fileio/MIDIFileReader.h',
  'svcore/data/fileio/MP3FileReader.h',
  'svcore/data/midi/MIDIInput.h',
  'svcore/data/model/AggregateWaveModel.h',
  'svcore/data/model/AlignmentModel.h',
  'svcore/data/model/BasicCompressedDenseThreeDimensionalModel.h',
  'svcore/data/model/Dense3DModelPeakCache.h',
  'svcore/data/model/DenseThreeDimensionalModel.h',
  'svcore/data/model/DenseTimeValueModel.h',
  'svcore/data/model/EditableDenseThreeDimensionalModel.h',
  'svcore/data/model/FFTModel.h',
  'svcore/data/model/ImageModel.h',
  'svcore/data/model/Labeller.h',
  'svcore/data/model/Model.h',
  'svcore/data/model/ModelDataTableModel.h',
  'svcore/data/model/NoteModel.h',
  'svcore/data/model/RangeSummarisableTimeValueModel.h',
  'svcore/data/model/RegionModel.h',
  'svcore/data/model/SparseOneDimensionalModel.h',
  'svcore/data/model/SparseTimeValueModel.h',
  'svcore/data/model/TextModel.h',
  'svcore/data/model/BoxModel.h',
  'svcore/data/model/WaveFileModel.h',
  'svcore/data/model/ReadOnlyWaveFileModel.h',
  'svcore/data/model/WritableWaveFileModel.h',
  'svcore/data/osc/OSCQueue.h',
  'svcore/rdf/RDFImporter.h',
  'svcore/rdf/RDFTransformFactory.h',
  'svcore/transform/FeatureExtractionModelTransformer.h',
  'svcore/transform/TransformFactory.h',
  'svcore/transform/ModelTransformerFactory.h',
],
  moc_extra_arguments: svcore_moc_args,
)

svcore_lib = static_library(
  'svcore',
  bq_files,
  vamphostsdk_files,
  dataquay_moc_files,
  svcore_moc_files,
  dataquay_files,
  svcore_files,
  include_directories: [
    all_include_dirs,
    feature_include_dirs,
  ],
  dependencies: [
    qt_dep,
    feature_dependencies,
    dl_dep,
  ],
  cpp_args: [
    feature_defines,
    general_defines,
  ],
)

svcore_dep = declare_dependency(
  link_with: svcore_lib,
  dependencies: [
  ],
  include_directories: [
    all_include_dirs,
    feature_include_dirs,
  ],
)

svcore_base_test_moc_files = qt.preprocess(
  moc_headers: [
  'svcore/base/test/TestById.h',
  'svcore/base/test/TestColumnOp.h',
  'svcore/base/test/TestLogRange.h',
  'svcore/base/test/TestMovingMedian.h',
  'svcore/base/test/TestOurRealTime.h',
  'svcore/base/test/TestPitch.h',
  'svcore/base/test/TestEventSeries.h',
  'svcore/base/test/TestRangeMapper.h',
  'svcore/base/test/TestScaleTickIntervals.h',
  'svcore/base/test/TestStringBits.h',
  'svcore/base/test/TestVampRealTime.h',
  'svcore/base/test/StressEventSeries.h',
],
  moc_extra_arguments: svcore_moc_args,
)

svcore_system_test_moc_files = qt.preprocess(
  moc_headers: [
  'svcore/system/test/TestEnv.h',
])

svcore_data_model_test_moc_files = qt.preprocess(
  moc_headers: [
  'svcore/data/model/test/MockWaveModel.h',
  'svcore/data/model/test/TestFFTModel.h',
  'svcore/data/model/test/TestSparseModels.h',
  'svcore/data/model/test/TestWaveformOversampler.h',
  'svcore/data/model/test/TestZoomConstraints.h',
])

svcore_data_fileio_test_moc_files = qt.preprocess(
  moc_headers: [
  'svcore/data/model/test/MockWaveModel.h',
  'svcore/data/fileio/test/AudioFileReaderTest.h',
  'svcore/data/fileio/test/BogusAudioFileReaderTest.h',
  'svcore/data/fileio/test/AudioFileWriterTest.h',
  'svcore/data/fileio/test/EncodingTest.h',
  'svcore/data/fileio/test/MIDIFileReaderTest.h',
  'svcore/data/fileio/test/CSVFormatTest.h',
  'svcore/data/fileio/test/CSVReaderTest.h',
  'svcore/data/fileio/test/CSVStreamWriterTest.h',
])

svgui_files = [
  'svgui/layer/Colour3DPlotExporter.cpp',
  'svgui/layer/Colour3DPlotLayer.cpp',
  'svgui/layer/Colour3DPlotRenderer.cpp',
  'svgui/layer/ColourDatabase.cpp',
  'svgui/layer/ColourMapper.cpp',
  'svgui/layer/ColourScale.cpp',
  'svgui/layer/CoordinateScale.cpp',
  'svgui/layer/FlexiNoteLayer.cpp',
  'svgui/layer/HorizontalFrequencyScale.cpp',
  'svgui/layer/ImageLayer.cpp',
  'svgui/layer/ImageRegionFinder.cpp',
  'svgui/layer/Layer.cpp',
  'svgui/layer/LayerFactory.cpp',
  'svgui/layer/LinearNumericalScale.cpp',
  'svgui/layer/LogNumericalScale.cpp',
  'svgui/layer/LinearColourScale.cpp',
  'svgui/layer/LogColourScale.cpp',
  'svgui/layer/NoteLayer.cpp',
  'svgui/layer/PaintAssistant.cpp',
  'svgui/layer/PianoScale.cpp',
  'svgui/layer/RegionLayer.cpp',
  'svgui/layer/ScrollableImageCache.cpp',
  'svgui/layer/ScrollableMagRangeCache.cpp',
  'svgui/layer/SingleColourLayer.cpp',
  'svgui/layer/SliceLayer.cpp',
  'svgui/layer/SpectrogramLayer.cpp',
  'svgui/layer/SpectrumLayer.cpp',
  'svgui/layer/TextLayer.cpp',
  'svgui/layer/TimeInstantLayer.cpp',
  'svgui/layer/BoxLayer.cpp',
  'svgui/layer/TimeRulerLayer.cpp',
  'svgui/layer/TimeValueLayer.cpp',
  'svgui/layer/WaveformLayer.cpp',
  'svgui/view/AlignmentView.cpp',
  'svgui/view/Overview.cpp',
  'svgui/view/Pane.cpp',
  'svgui/view/PaneStack.cpp',
  'svgui/view/View.cpp',
  'svgui/view/ViewManager.cpp',
  'svgui/widgets/ActivityLog.cpp',
  'svgui/widgets/AudioDial.cpp',
  'svgui/widgets/ColourComboBox.cpp',
  'svgui/widgets/ColourMapComboBox.cpp',
  'svgui/widgets/ColourNameDialog.cpp',
  'svgui/widgets/CommandHistory.cpp',
  'svgui/widgets/CSVAudioFormatDialog.cpp',
  'svgui/widgets/CSVExportDialog.cpp',
  'svgui/widgets/CSVFormatDialog.cpp',
  'svgui/widgets/Fader.cpp',
  'svgui/widgets/InteractiveFileFinder.cpp',
  'svgui/widgets/IconLoader.cpp',
  'svgui/widgets/ImageDialog.cpp',
  'svgui/widgets/ItemEditDialog.cpp',
  'svgui/widgets/KeyReference.cpp',
  'svgui/widgets/LabelCounterInputDialog.cpp',
  'svgui/widgets/LayerTree.cpp',
  'svgui/widgets/LayerTreeDialog.cpp',
  'svgui/widgets/LEDButton.cpp',
  'svgui/widgets/LevelPanToolButton.cpp',
  'svgui/widgets/LevelPanWidget.cpp',
  'svgui/widgets/ListInputDialog.cpp',
  'svgui/widgets/MIDIFileImportDialog.cpp',
  'svgui/widgets/ModelDataTableDialog.cpp',
  'svgui/widgets/NotifyingCheckBox.cpp',
  'svgui/widgets/NotifyingComboBox.cpp',
  'svgui/widgets/NotifyingPushButton.cpp',
  'svgui/widgets/NotifyingTabBar.cpp',
  'svgui/widgets/NotifyingToolButton.cpp',
  'svgui/widgets/Panner.cpp',
  'svgui/widgets/PluginParameterBox.cpp',
  'svgui/widgets/PluginParameterDialog.cpp',
  'svgui/widgets/PluginPathConfigurator.cpp',
  'svgui/widgets/PluginReviewDialog.cpp',
  'svgui/widgets/ProgressDialog.cpp',
  'svgui/widgets/PropertyBox.cpp',
  'svgui/widgets/PropertyStack.cpp',
  'svgui/widgets/RangeInputDialog.cpp',
  'svgui/widgets/SelectableLabel.cpp',
  'svgui/widgets/SubdividingMenu.cpp',
  'svgui/widgets/TextAbbrev.cpp',
  'svgui/widgets/Thumbwheel.cpp',
  'svgui/widgets/TransformFinder.cpp',
  'svgui/widgets/UnitConverter.cpp',
  'svgui/widgets/WindowShapePreview.cpp',
  'svgui/widgets/WindowTypeSelector.cpp',
]

svgui_moc_files = qt.preprocess(
  moc_headers: [
  'svgui/layer/Colour3DPlotExporter.h',
  'svgui/layer/Colour3DPlotLayer.h',
  'svgui/layer/ColourDatabase.h',
  'svgui/layer/FlexiNoteLayer.h',
  'svgui/layer/ImageLayer.h',
  'svgui/layer/Layer.h',
  'svgui/layer/NoteLayer.h',
  'svgui/layer/RegionLayer.h',
  'svgui/layer/SingleColourLayer.h',
  'svgui/layer/SliceableLayer.h',
  'svgui/layer/SliceLayer.h',
  'svgui/layer/SpectrogramLayer.h',
  'svgui/layer/SpectrumLayer.h',
  'svgui/layer/TextLayer.h',
  'svgui/layer/TimeInstantLayer.h',
  'svgui/layer/BoxLayer.h',
  'svgui/layer/TimeRulerLayer.h',
  'svgui/layer/TimeValueLayer.h',
  'svgui/layer/WaveformLayer.h',
  'svgui/view/AlignmentView.h',
  'svgui/view/Overview.h',
  'svgui/view/Pane.h',
  'svgui/view/PaneStack.h',
  'svgui/view/View.h',
  'svgui/view/ViewManager.h',
  'svgui/widgets/ActivityLog.h',
  'svgui/widgets/AudioDial.h',
  'svgui/widgets/ClickableLabel.h',
  'svgui/widgets/ColourComboBox.h',
  'svgui/widgets/ColourMapComboBox.h',
  'svgui/widgets/ColourNameDialog.h',
  'svgui/widgets/CommandHistory.h',
  'svgui/widgets/CSVAudioFormatDialog.h',
  'svgui/widgets/CSVExportDialog.h',
  'svgui/widgets/CSVFormatDialog.h',
  'svgui/widgets/Fader.h',
  'svgui/widgets/InteractiveFileFinder.h',
  'svgui/widgets/ImageDialog.h',
  'svgui/widgets/ItemEditDialog.h',
  'svgui/widgets/KeyReference.h',
  'svgui/widgets/LabelCounterInputDialog.h',
  'svgui/widgets/LayerTree.h',
  'svgui/widgets/LayerTreeDialog.h',
  'svgui/widgets/LEDButton.h',
  'svgui/widgets/LevelPanToolButton.h',
  'svgui/widgets/LevelPanWidget.h',
  'svgui/widgets/ListInputDialog.h',
  'svgui/widgets/MIDIFileImportDialog.h',
  'svgui/widgets/ModelDataTableDialog.h',
  'svgui/widgets/NotifyingCheckBox.h',
  'svgui/widgets/NotifyingComboBox.h',
  'svgui/widgets/NotifyingPushButton.h',
  'svgui/widgets/NotifyingTabBar.h',
  'svgui/widgets/NotifyingToolButton.h',
  'svgui/widgets/Panner.h',
  'svgui/widgets/PluginParameterBox.h',
  'svgui/widgets/PluginParameterDialog.h',
  'svgui/widgets/PluginPathConfigurator.h',
  'svgui/widgets/PluginReviewDialog.h',
  'svgui/widgets/ProgressDialog.h',
  'svgui/widgets/PropertyBox.h',
  'svgui/widgets/PropertyStack.h',
  'svgui/widgets/RangeInputDialog.h',
  'svgui/widgets/SelectableLabel.h',
  'svgui/widgets/SubdividingMenu.h',
  'svgui/widgets/Thumbwheel.h',
  'svgui/widgets/TransformFinder.h',
  'svgui/widgets/UnitConverter.h',
  'svgui/widgets/WindowShapePreview.h',
  'svgui/widgets/WindowTypeSelector.h',
])

svapp_files = [
  'svapp/align/Align.cpp',
  'svapp/align/ExternalProgramAligner.cpp',
  'svapp/align/LinearAligner.cpp',
  'svapp/align/MATCHAligner.cpp',
  'svapp/align/TransformDTWAligner.cpp',
  'svapp/audio/AudioCallbackPlaySource.cpp',
  'svapp/audio/AudioCallbackRecordTarget.cpp',
  'svapp/audio/AudioGenerator.cpp',
  'svapp/audio/ClipMixer.cpp',
  'svapp/audio/ContinuousSynth.cpp',
  'svapp/audio/EffectWrapper.cpp',
  'svapp/audio/PlaySpeedRangeMapper.cpp',
  'svapp/audio/TimeStretchWrapper.cpp',
  'svapp/framework/Document.cpp',
  'svapp/framework/MainWindowBase.cpp',
  'svapp/framework/SVFileReader.cpp',
  'svapp/framework/TransformUserConfigurator.cpp',
  'svapp/framework/VersionTester.cpp',
]

svapp_moc_files = qt.preprocess(
  moc_headers: [
  'svapp/align/Align.h',
  'svapp/align/Aligner.h',
  'svapp/align/ExternalProgramAligner.h',
  'svapp/align/LinearAligner.h',
  'svapp/align/MATCHAligner.h',
  'svapp/align/TransformDTWAligner.h',
  'svapp/audio/AudioCallbackPlaySource.h',
  'svapp/audio/AudioCallbackRecordTarget.h',
  'svapp/audio/AudioGenerator.h',
  'svapp/framework/Document.h',
  'svapp/framework/MainWindowBase.h',
  'svapp/framework/OSCScript.h',
  'svapp/framework/SVFileReader.h',
  'svapp/framework/VersionTester.h',
])

pyin_files = [
  'pyin/YinUtil.cpp',
  'pyin/Yin.cpp',
  'pyin/SparseHMM.cpp',
  'pyin/MonoPitchHMM.cpp',
  'pyin/MonoNoteParameters.cpp',
  'pyin/MonoNoteHMM.cpp',
  'pyin/MonoNote.cpp',
  'pyin/libmain.cpp',
  'pyin/YinVamp.cpp',
  'pyin/PYinVamp.cpp',
  'pyin/LocalCandidatePYIN.cpp',
  'vamp-plugin-sdk/src/vamp-sdk/FFT.cpp',
  'vamp-plugin-sdk/src/vamp-sdk/PluginAdapter.cpp',
  'vamp-plugin-sdk/src/vamp-sdk/RealTime.cpp',
]

pyin_plugin = shared_library(
  'pyin',
  pyin_files,
  include_directories: [
    'vamp-plugin-sdk',
  ],
  cpp_args: [
    general_defines,
  ],
  link_args: [
    general_link_args,
    vamp_symbol_args,
  ],
  dependencies: boost_dep,
  name_prefix: '',
  install: true,
)

chp_files = [
  'chp/ConstrainedHarmonicPeak.cpp',
  'chp/plugins.cpp',
  'vamp-plugin-sdk/src/vamp-sdk/FFT.cpp',
  'vamp-plugin-sdk/src/vamp-sdk/PluginAdapter.cpp',
  'vamp-plugin-sdk/src/vamp-sdk/RealTime.cpp',
]

chp_plugin = shared_library(
  'chp',
  chp_files,
  include_directories: [
    'vamp-plugin-sdk',
  ],
  cpp_args: [
    general_defines,
  ],
  link_args: [
    general_link_args,
    vamp_symbol_args,
  ],
  name_prefix: '',
  install: true,
)

# main.cpp is kept apart from the rest of main/ so that the rest can be
# built once, as static libraries, and linked into both the application
# and the test executables (see main/test/).
tony_entry_files = [
  'main/main.cpp',
]

# No GUI dependencies: usable from a QCoreApplication test.
tony_core_files = [
  'main/AndroidFiles.cpp',
  'main/AudioDriverSettings.cpp',
  'main/Coverage.cpp',
  'main/DecodedPcm.cpp',
  'main/LogFile.cpp',
  'main/LatencyCalibration.cpp',
  'main/LatencyCheck.cpp',
  'main/LiveDotsFeed.cpp',
  'main/Lyrics.cpp',
  'main/LyricsEdit.cpp',
  'main/LyricsTtml.cpp',
  'main/PinchZoom.cpp',
  'main/PopupArea.cpp',
  'main/RealtimePitchTracker.cpp',
  'main/SingingTakes.cpp',
  'main/StreamLatency.cpp',
  'main/TakeAudio.cpp',
  'main/TakeDiff.cpp',
  'main/TakeEvents.cpp',
  'main/TakesFile.cpp',
  'main/TakeTiming.cpp',
  'main/VerticalZoom.cpp',
]

# Android's decoders for the audio its libraries cannot read (M4A, FLAC,
# Ogg): a bqaudiostream reader that registers itself, which only link_whole
# keeps in the application, nothing referring to it
if system == 'android'
  tony_core_files += [
    'main/AndroidMediaReadStream.cpp',
  ]
endif

tony_app_files = [
  'main/AlternatePitchTrack.cpp',
  'main/AudioCheckRunner.cpp',
  'main/AudioDriverMenus.cpp',
  'main/CalibrateAudioDialog.cpp',
  'main/CompactLayout.cpp',
  'main/CoverageStrip.cpp',
  'main/LyricsTrack.cpp',
  'main/LyricsEditor.cpp',
  'main/Analyser.cpp',
  'main/MainWindow.cpp',
  'main/NetworkPermissionTester.cpp',
  'main/PaneUtils.cpp',
  'main/PlotSize.cpp',
  'main/TakeCommands.cpp',
  'main/TakeLayers.cpp',
  'main/TouchGestures.cpp',
  'main/TouchMenuStyle.cpp',
]

if system == 'android'
  tony_app_files += [
    'main/AndroidStorage.cpp',
    'main/OboeAudioIO.cpp',
  ]
endif

tony_core_moc_files = qt.preprocess(
  moc_headers: [
  'main/RealtimePitchTracker.h',
  'main/SingingTakes.h',
])

tony_app_moc_headers = [
  'main/MainWindow.h',
  'main/Analyser.h',
  'main/AlternatePitchTrack.h',
  'main/AudioCheckRunner.h',
  'main/AudioDriverMenus.h',
  'main/CalibrateAudioDialog.h',
  'main/CompactLayout.h',
  'main/CoverageStrip.h',
  'main/LyricsTrack.h',
  'main/LyricsEditor.h',
  'main/PlotSize.h',
  'main/TouchGestures.h',
  'main/TouchMenuStyle.h',
]

if dev_checks
  tony_app_files += [
    'main/dev/DevChecks.cpp',
    'main/dev/TakeObserver.cpp',
  ]
  tony_app_moc_headers += [
    'main/dev/DevChecks.h',
    'main/dev/TakeObserver.h',
  ]
endif

tony_app_moc_files = qt.preprocess(
  moc_headers: tony_app_moc_headers,
  moc_extra_arguments: dev_moc_args,
)

qt_resource_files = qt.preprocess(
  qresources: [
  'tony.qrc',
])

if compiler == 'msvc'
  os = shared_library(
    'os',
    [ 'svcore/system/os-win10.cpp' ],
    cpp_args: [
      general_defines,
    ],
    link_args: [
      '-EXPORT:OSReportsDarkThemeActive',
      '-EXPORT:OSQueryAccentColour',
      '-lWindowsApp',
      general_link_args,
    ])
  shared_library(
    'os_other',
    [ 'svcore/system/os-other.cpp' ],
    cpp_args: [
      general_defines,
    ],
    link_args: [
      '-EXPORT:OSReportsDarkThemeActive',
      '-EXPORT:OSQueryAccentColour',
      general_link_args,
    ])
  os_dep = declare_dependency(
    link_with: os
  )
else
  os_dep = []
endif

if system == 'linux'
  tony_main_name = 'tony'
elif system == 'android'
  # androiddeployqt looks for lib<application-binary>_<abi>.so, and the
  # deployment settings name the application-binary Tony
  tony_main_name = 'Tony_' + android_abi
else
  tony_main_name = 'Tony'
endif # system

install_data(
  'tony.desktop',
  install_dir : get_option('datadir') / 'applications',
)

install_data(
  'icons/tony-icon.svg',
  install_dir : get_option('datadir') / 'icons/hicolor/scalable/apps',
)

tony_core_lib = static_library(
  'tonycore',
  tony_core_moc_files,
  tony_core_files,
  dependencies: [
    svcore_dep,
    qt_dep,
    feature_dependencies,
  ],
  cpp_args: [
    feature_defines,
    general_defines,
  ],
)

tony_core_dep = declare_dependency(
  link_with: tony_core_lib,
  include_directories: [ 'main' ],
)

tony_app_lib = static_library(
  'tonyapp',
  svgui_moc_files,
  svapp_moc_files,
  tony_app_moc_files,
  svgui_files,
  svapp_files,
  tony_app_files,
  dependencies: [
    svcore_dep,
    qt_dep,
    feature_dependencies,
  ],
  cpp_args: [
    feature_defines,
    general_defines,
  ],
)

tony_app_dep = declare_dependency(
  link_with: tony_app_lib,
  include_directories: [ 'main' ],
)

# link_whole so that the application contains exactly the objects it did
# when these sources were compiled straight into it.
if system == 'android'
  # Qt for Android's Java launcher loads the application as a shared
  # library and calls its main(), the one symbol it has to export.
  # --exclude-libs keeps whatever comes from a static library (Tony's
  # own, svcore, the C libraries) out of its dynamic symbol table.
  shared_library(
    tony_main_name,
    qt_resource_files,
    tony_entry_files,
    link_whole: [
      tony_app_lib,
      tony_core_lib,
    ],
    dependencies: [
      svcore_dep,
      qt_dep,
      feature_dependencies,
      os_dep,
      dl_dep
    ],
    cpp_args: [
      feature_defines,
      general_defines,
    ],
    link_args: [
      feature_additional_libs,
      general_link_args,
      '-Wl,--exclude-libs,ALL',
    ],
    install: true,
  )
else
  executable(
    tony_main_name,
    qt_resource_files,
    tony_entry_files,
    rc,
    link_whole: [
      tony_app_lib,
      tony_core_lib,
    ],
    dependencies: [
      svcore_dep,
      qt_dep,
      feature_dependencies,
      os_dep,
      dl_dep
    ],
    cpp_args: [
      feature_defines,
      general_defines,
    ],
    link_args: [
      feature_additional_libs,
      general_link_args,
    ],
    win_subsystem: 'windows',
    install: true,
  )
endif # system

# The tests run on the desktop only: the Android build is the
# application and its plugins.
if system != 'android'

  svcore_base_test_exe = executable(
    'test-svcore-base',
    svcore_base_test_moc_files,
    'svcore/base/test/svcore-base-test.cpp',
    dependencies: [
      svcore_dep,
      qt_dep,
      feature_dependencies,
      dl_dep,
    ],
    cpp_args: [
      feature_defines,
      general_defines,
    ],
    link_args: [
      feature_additional_libs,
      general_link_args,
    ],
    win_subsystem: 'console'
  )

  svcore_system_test_exe = executable(
    'test-svcore-system',
    svcore_system_test_moc_files,
    'svcore/system/test/svcore-system-test.cpp',
    dependencies: [
      svcore_dep,
      qt_dep,
      feature_dependencies,
      dl_dep,
    ],
    cpp_args: [
      feature_defines,
      general_defines,
    ],
    link_args: [
      feature_additional_libs,
      general_link_args,
    ],
    win_subsystem: 'console'
  )

  svcore_data_model_test_exe = executable(
    'test-svcore-data-model',
    svcore_data_model_test_moc_files,
    'svcore/data/model/test/MockWaveModel.cpp',
    'svcore/data/model/test/svcore-data-model-test.cpp',
    dependencies: [
      svcore_dep,
      qt_dep,
      feature_dependencies,
      dl_dep,
    ],
    cpp_args: [
      feature_defines,
      general_defines,
    ],
    link_args: [
      feature_additional_libs,
      general_link_args,
    ],
    win_subsystem: 'console'
  )

  svcore_data_fileio_test_exe = executable(
    'test-svcore-data-fileio',
    svcore_data_fileio_test_moc_files,
    'svcore/data/model/test/MockWaveModel.cpp',
    'svcore/data/fileio/test/UnsupportedFormat.cpp',
    'svcore/data/fileio/test/svcore-data-fileio-test.cpp',
    dependencies: [
      svcore_dep,
      qt_dep,
      feature_dependencies,
      dl_dep,
    ],
    cpp_args: [
      feature_defines,
      general_defines,
    ],
    link_args: [
      feature_additional_libs,
      general_link_args,
    ],
    win_subsystem: 'console'
  )

  # Tony's own tests. Two executables, because they differ in cost and in
  # what they link: test-tony-core needs no GUI and no audio device;
  # test-tony-app creates widgets (offscreen) and links svgui and svapp.

  tony_core_test_moc_files = qt.preprocess(
    moc_headers: [
    'main/test/TestAndroidFiles.h',
    'main/test/TestRealtimeYin.h',
    'main/test/TestRealtimePitchTracker.h',
    'main/test/TestLatencyShift.h',
    'main/test/TestCoverage.h',
    'main/test/TestDecodedPcm.h',
    'main/test/TestLogFile.h',
    'main/test/TestPinchZoom.h',
    'main/test/TestPopupArea.h',
    'main/test/TestStreamLatency.h',
    'main/test/TestTakeAudio.h',
    'main/test/TestTakeEvents.h',
    'main/test/TestSingingTakes.h',
    'main/test/TestTakesFile.h',
    'main/test/TestTakeTiming.h',
    'main/test/TestVerticalZoom.h',
    'main/test/TestLyrics.h',
    'main/test/TestLyricsTtml.h',
    'main/test/TestLyricsEdit.h',
    'main/test/TestLatencyCheck.h',
    'main/test/TestLatencyCalibration.h',
    'main/test/TestAudioDriverSettings.h',
    'main/test/TestTakeDiff.h',
    'main/test/TestLiveDotsFeed.h',
    'main/test/TestRunSuite.h',
  ])

  # Where the suites find the files in testdata/. Forward slashes: a
  # backslash of a Windows path would start an escape in the C string.
  tony_test_data_dir = import('fs').as_posix(meson.current_source_dir() / 'testdata')

  tony_core_test_exe = executable(
    'test-tony-core',
    tony_core_test_moc_files,
    'main/test/PyinReference.cpp',
    'pyin/YinUtil.cpp',
    'vamp-plugin-sdk/src/vamp-sdk/FFT.cpp',
    'main/test/tony-core-test.cpp',
    dependencies: [
      tony_core_dep,
      svcore_dep,
      qt_dep,
      feature_dependencies,
      dl_dep,
    ],
    cpp_args: [
      feature_defines,
      general_defines,
      '-DTONY_TEST_DATA_DIR="' + tony_test_data_dir + '"',
    ],
    link_args: [
      feature_additional_libs,
      general_link_args,
    ],
    win_subsystem: 'console'
  )

  tony_app_test_moc_headers = [
    'main/test/TestSingingDocument.h',
    'main/test/TestViewCache.h',
    'main/test/TestSingingAnalysis.h',
    'main/test/TestRecordWorkflow.h',
    'main/test/TestLyricsLayer.h',
    'main/test/TestPlotSize.h',
    'main/test/TestUiChecks.h',
    'main/test/TestAudioCheck.h',
    'main/test/TestTouchGestures.h',
    'main/test/TestCompactLayout.h',
    'main/test/TestTouchMenuStyle.h',
  ]

  tony_app_test_moc_files = qt.preprocess(
    moc_headers: tony_app_test_moc_headers,
    moc_extra_arguments: dev_moc_args,
  )

  tony_app_test_exe = executable(
    'test-tony-app',
    tony_app_test_moc_files,
    'main/test/tony-app-test.cpp',
    # The icons: without them every toolbar button shows its text, and
    # is as wide as that (TestCompactLayout needs the buttons' real size)
    qt_resource_files,
    dependencies: [
      tony_app_dep,
      tony_core_dep,
      svcore_dep,
      qt_dep,
      feature_dependencies,
      os_dep,
      dl_dep,
    ],
    cpp_args: [
      feature_defines,
      general_defines,
      '-DTONY_TEST_DATA_DIR="' + tony_test_data_dir + '"',
    ],
    link_args: [
      feature_additional_libs,
      general_link_args,
    ],
    win_subsystem: 'console'
  )

  test('tony-core', tony_core_test_exe)
  test('tony-app', tony_app_test_exe,
       depends: pyin_plugin,
       timeout: 900,
       env: [ 'QT_QPA_PLATFORM=offscreen' ])

  # The development checks' suite, only where the checks are built. Its
  # own executable: every test records takes in real time, and it is run
  # when a change touches what the checks drive (AGENTS.md)
  if dev_checks
    tony_dev_test_moc_files = qt.preprocess(
      moc_headers: [
      'main/test/TestDevChecks.h',
      ],
      moc_extra_arguments: dev_moc_args,
    )

    tony_dev_test_exe = executable(
      'test-tony-dev',
      tony_dev_test_moc_files,
      'main/test/tony-dev-test.cpp',
      dependencies: [
        tony_app_dep,
        tony_core_dep,
        svcore_dep,
        qt_dep,
        feature_dependencies,
        os_dep,
        dl_dep,
      ],
      cpp_args: [
        feature_defines,
        general_defines,
      ],
      link_args: [
        feature_additional_libs,
        general_link_args,
      ],
      win_subsystem: 'console'
    )

    test('tony-dev', tony_dev_test_exe,
         depends: pyin_plugin,
         timeout: 900,
         env: [ 'QT_QPA_PLATFORM=offscreen' ])
  endif

  test('svcore-base', svcore_base_test_exe)
  test('svcore-system', svcore_system_test_exe)
  test('svcore-data-model', svcore_data_model_test_exe)
  test('svcore-data-fileio', svcore_data_fileio_test_exe,
       args: [
         '--testdir', meson.current_source_dir() / 'svcore/data/fileio/test'
       ])

endif # system

summary({'prefix': get_option('prefix'),
         'bindir': get_option('bindir'),
         'libdir': get_option('libdir'),
         'datadir': get_option('datadir'),
        }, section: 'Directories')

summary(config_summary, section: 'Configuration', bool_yn: true)
