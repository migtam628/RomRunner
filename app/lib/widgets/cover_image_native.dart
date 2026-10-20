import 'dart:io';

import 'package:flutter/widgets.dart';

ImageProvider coverImageProvider(String path) => FileImage(File(path));
