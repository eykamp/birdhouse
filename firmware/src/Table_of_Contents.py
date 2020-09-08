entries = ["Title Page", "Table of Contents", "Introduction", "Background", "Experimental Setup", "Hypothesis", "Qualitative Data Analysis", "Quantitative Data Analysis",
           "    Flipping Point", "    Maximum Angular Velocity in the Non-Intermediate Axes", "    Second Order Differences", "Conclusion", "Extension", "Works Cited", "Appendix"]

pages = [1, 2, 3, 4, 5, 8, 11, 14, 16, 18, 23, 26, 27, 28, 29]

delimeter = ". " 

for i in range(len(entries)):
    if len(entries[i]) % 2 == 1:
        print(entries[i] + "  " + delimeter * int((40 - len(entries[i]) / 2)) + " " + str(pages[i]))
    else:
        print(entries[i] + " " + delimeter * int((40 - len(entries[i]) / len(delimeter))) + " " + str(pages[i]))
