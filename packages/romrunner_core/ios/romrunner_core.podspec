Pod::Spec.new do |s|
  s.name             = 'romrunner_core'
  s.version          = '0.1.0'
  s.summary          = 'libretro bridge for RomRunner.'
  s.description      = 'Hosts libretro cores, drives audio and converts frames for the RomRunner Flutter app.'
  s.homepage         = 'https://mttechservices.local'
  s.license          = { :type => 'Proprietary' }
  s.author           = { 'MT Tech Services' => 'migtam055@gmail.com' }
  s.source           = { :path => '.' }
  # Classes/romrunner_core.m includes ../../src/romrunner_core.c so both
  # platforms share one source. It is .m so miniaudio can use AVFoundation.
  s.source_files     = 'Classes/**/*'
  s.requires_arc     = false
  s.dependency 'Flutter'
  s.platform         = :ios, '15.0'
  s.frameworks       = 'AVFoundation', 'AudioToolbox', 'CoreAudio'
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386',
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) DART_SHARED_LIB=1',
  }
  s.swift_version = '5.0'
end
