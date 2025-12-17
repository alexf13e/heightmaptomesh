# Heightmap to Mesh
A program for taking in an image and creating a 3D model where the height of each pixel in the model is the grayscale value. The model will output square columns for each pixel, so is not suitable for a smoother terrain mesh.

## Usage
Run the program and enter the information as prompted:
* Input image - the image which contains the values which should be used as heights.
* Number of colours/layers - the number of discrete height values the image will be quantised to. Minimum of 2 so that zero and non-zero can be repesented (e.g. for 2 layers, pixel values 0-127 become layer 1, 128-255 become layer 2).
* Layer height - the number of units one layer (y axis) will be, mm are referenced as the program was made for testing 3D printing.
* Model width - the number of units the width (x axis) of the output model will have. The depth (z axis) will adjust automatically to maintain the image's aspect ratio.
* Enable dithering - Floyd-Steinberg dithering can be used to ease colour banding for 3D printing, but makes the model very complex and noisy (since it reduces the effectiveness of greedy meshing which is used to remove unnecessary points from the mesh).
* Output mesh - the location to save the output obj file to.

## Build dependencies
* Native File Dialog Extended - https://github.com/btzy/nativefiledialog-extended
