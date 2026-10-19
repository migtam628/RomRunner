Pod::Spec.new do |s|
  s.name             = 'migplay_core'
  s.version          = '0.1.0'
  s.summary          = 'libretro bridge for MigPlay.'
  s.description      = 'Hosts libretro cores, drives audio and converts frames for the MigPlay Flutter app.'
  s.homepage         = 'https://mttechservices.local'
  s.license          = { :type => 'Proprietary' }
  s.author           = { 'MT Tech Services' => 'migtam055@gmail.com' }
  s.source           = { :path => '.' }
  s.source_files     = 'Classes/**/*'
  s.dependency 'FlutterMacOS'
  s.platform         = :osx, '10.15'
  s.frameworks       = 'CoreAudio', 'AudioToolbox', 'CoreFoundation'
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) DART_SHARED_LIB=1',
  }
  s.swift_version = '5.0'
end
