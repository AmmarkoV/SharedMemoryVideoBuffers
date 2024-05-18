import cv2
import numpy as np

def main():
    # Open the first webcam connected to the computer
    cap = cv2.VideoCapture(0)
    
    # Check if the webcam is opened successfully
    if not cap.isOpened():
        print("Error: Couldn't open webcam")
        return
    
    # Loop to continuously read frames from the webcam
    while True:
        # Capture frame-by-frame
        ret, frame = cap.read()
        
        # Check if the frame is captured successfully
        if not ret:
            print("Error: Couldn't read frame from webcam")
            break
        
        # Display the frame in a window
        cv2.imshow('Webcam', frame)
        
        # Break the loop if 'q' is pressed
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break
    
    # Release the webcam and close all OpenCV windows
    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()

