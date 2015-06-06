all:
	./buildepoch.sh
clean:
	rm -rf built objects
	rm -f src/*.o src/*.gch
