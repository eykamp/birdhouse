import pandas as pd			# pip install pandas
import json
import itertools

from bokeh.models import ColumnDataSource		# pip install bokeh
from bokeh.plotting import figure, show, output_file

# select a palette
from bokeh.palettes import Category10 as palette


from testData import telemetry, reference_telemetry

def create_data_frame(data, name):
    df = (pd.read_json(json.dumps(data), orient="records", typ="frame", convert_dates=['ts'])
              .set_index('ts')
              .sort_values(by=['ts'])) 

    df.rename(columns={"value":name}, inplace=True)

    return df

def add_line(p, birdhouse_number, color, batch=None):
	tel = telemetry[birdhouse_number] if birdhouse_number != 'DEQ' else reference_telemetry["002" if batch == 1 else "007"] 
	leg = 'birdhouse {}'.format(birdhouse_number) if birdhouse_number != 'DEQ' else 'DEQ reference'
	p.line('ts', birdhouse_number, source=ColumnDataSource(create_data_frame(tel, birdhouse_number)), color=color, legend=leg)
	


plot_width = 1600
plot_height = 500

p1 = figure(x_axis_type="datetime", plot_width=plot_width, plot_height=plot_height)
p2 = figure(x_axis_type="datetime", plot_width=plot_width, plot_height=plot_height)

color_gen = iter(palette[10])
add_line(p1, "DEQ", next(color_gen), 1)
add_line(p1, "002", next(color_gen))
add_line(p1, "003", next(color_gen))
add_line(p1, "004", next(color_gen))
add_line(p1, "005", next(color_gen))
add_line(p1, "010", next(color_gen))
add_line(p1, "011", next(color_gen))
add_line(p1, "012", next(color_gen))
add_line(p1, "013", next(color_gen))

color_gen = iter(palette[10])
add_line(p2, "DEQ", next(color_gen), 2)
add_line(p2, "007", next(color_gen))
add_line(p2, "039", next(color_gen))
add_line(p2, "009", next(color_gen))
add_line(p2, "006", next(color_gen))
add_line(p2, "020", next(color_gen))
add_line(p2, "021", next(color_gen))
add_line(p2, "024", next(color_gen))
add_line(p2, "018", next(color_gen))



output_file("birdhouse_data_plots.html")
show(p2)
show(p1)